// Solis Programming Language - Interpreter Implementation
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/interpreter/interpreter.hpp"

#include "cli/module/module_resolver.hpp"
#include "cli/module/namespace_manager.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace solis {

template <class... Ts>
struct overload : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

// Helper to convert Value to String (Show typeclass behavior)
std::string valToString(const ValuePtr& val);

std::string valToString(const ValuePtr& val) {
  return std::visit(overload{[](const StringValue& s) { return "\"" + s.value + "\""; },
                             [](const IntValue& i) { return std::to_string(i.value); },
                             [](const FloatValue& f) { return std::to_string(f.value); },
                             [](const BoolValue& b) {
                               return b.value ? std::string("true") : std::string("false");
                             },
                             [](const ConstructorValue& c) {
                               std::string res = c.name;
                               for (const auto& arg : c.args) {
                                 std::string argStr = valToString(arg);
                                 // Add parens if argument is complex (contains spaces)
                                 // This is a heuristic; proper precedence handling would be better
                                 if (argStr.find(' ') != std::string::npos &&
                                     argStr.front() != '"' && argStr.front() != '[') {
                                   res += " (" + argStr + ")";
                                 } else {
                                   res += " " + argStr;
                                 }
                               }
                               return res;
                             },
                             [](const ListValue& l) {
                               std::string res = "[";
                               for (size_t i = 0; i < l.elements.size(); ++i) {
                                 if (i > 0)
                                   res += ", ";
                                 res += valToString(l.elements[i]);
                               }
                               res += "]";
                               return res;
                             },
                             [](const auto&) {
                               return std::string("<value>");
                             }},
                    val->data);
}

Interpreter::Interpreter() {
  // Add built-in functions
  globalEnv_["print"] = std::make_shared<Value>(Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
    if (auto* s = std::get_if<StringValue>(&arg->data)) {
      std::cout << s->value << std::endl;
    } else {
      std::cout << valToString(arg) << std::endl;
    }
    return std::make_shared<Value>(Value{BoolValue{true}});
  }}});

  globalEnv_["show"] = std::make_shared<Value>(Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
    return std::make_shared<Value>(Value{StringValue{valToString(arg)}});
  }}});

  // NATIVE STRING OPERATIONS (Phase 1 Fix)

  // words: String -> [String]
  globalEnv_["words"] = std::make_shared<Value>(Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
    if (auto* s = std::get_if<StringValue>(&arg->data)) {
      std::vector<ValuePtr> words;
      std::string current;
      for (char c : s->value) {
        if (std::isspace(c)) {
          if (!current.empty()) {
            words.push_back(std::make_shared<Value>(Value{StringValue{current}}));
            current.clear();
          }
        } else {
          current += c;
        }
      }
      if (!current.empty()) {
        words.push_back(std::make_shared<Value>(Value{StringValue{current}}));
      }
      return std::make_shared<Value>(Value{ListValue{words}});
    }
    throw std::runtime_error("words expects a String");
  }}});

  // unwords: [String] -> String
  globalEnv_["unwords"] = std::make_shared<Value>(Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
    if (auto* l = std::get_if<ListValue>(&arg->data)) {
      std::string result;
      for (size_t i = 0; i < l->elements.size(); ++i) {
        if (i > 0)
          result += " ";
        if (auto* s = std::get_if<StringValue>(&l->elements[i]->data)) {
          result += s->value;
        } else {
          throw std::runtime_error("unwords expects a list of Strings");
        }
      }
      return std::make_shared<Value>(Value{StringValue{result}});
    }
    throw std::runtime_error("unwords expects a list of Strings");
  }}});

  // lines: String -> [String]
  globalEnv_["lines"] = std::make_shared<Value>(Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
    if (auto* s = std::get_if<StringValue>(&arg->data)) {
      std::vector<ValuePtr> lines;
      std::string current;
      for (char c : s->value) {
        if (c == '\n') {
          lines.push_back(std::make_shared<Value>(Value{StringValue{current}}));
          current.clear();
        } else {
          current += c;
        }
      }
      if (!current.empty()) {
        lines.push_back(std::make_shared<Value>(Value{StringValue{current}}));
      }
      return std::make_shared<Value>(Value{ListValue{lines}});
    }
    throw std::runtime_error("lines expects a String");
  }}});

  // unlines: [String] -> String
  globalEnv_["unlines"] = std::make_shared<Value>(Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
    if (auto* l = std::get_if<ListValue>(&arg->data)) {
      std::string result;
      for (const auto& elem : l->elements) {
        if (auto* s = std::get_if<StringValue>(&elem->data)) {
          result += s->value + "\n";
        } else {
          throw std::runtime_error("unlines expects a list of Strings");
        }
      }
      return std::make_shared<Value>(Value{StringValue{result}});
    }
    throw std::runtime_error("unlines expects a list of Strings");
  }}});

  // trim: String -> String
  globalEnv_["trim"] = std::make_shared<Value>(Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
    if (auto* s = std::get_if<StringValue>(&arg->data)) {
      std::string str = s->value;
      const char* ws = " \t\n\r\f\v";
      str.erase(str.find_last_not_of(ws) + 1);
      str.erase(0, str.find_first_not_of(ws));
      return std::make_shared<Value>(Value{StringValue{str}});
    }
    throw std::runtime_error("trim expects a String");
  }}});

  // startsWith: String -> String -> Bool
  globalEnv_["startsWith"] = std::make_shared<Value>(
      Value{FunctionValue{[](ValuePtr prefixArg) -> ValuePtr {
        return std::make_shared<Value>(
            Value{FunctionValue{[prefixArg](ValuePtr strArg) -> ValuePtr {
              if (auto* prefix = std::get_if<StringValue>(&prefixArg->data)) {
                if (auto* str = std::get_if<StringValue>(&strArg->data)) {
                  bool res = str->value.rfind(prefix->value, 0) == 0;
                  return std::make_shared<Value>(Value{BoolValue{res}});
                }
              }
              throw std::runtime_error("startsWith expects two Strings");
            }}});
      }}});

  // endsWith: String -> String -> Bool
  globalEnv_["endsWith"] = std::make_shared<Value>(Value{FunctionValue{[](ValuePtr suffixArg)
                                                                           -> ValuePtr {
    return std::make_shared<Value>(Value{FunctionValue{[suffixArg](ValuePtr strArg) -> ValuePtr {
      if (auto* suffix = std::get_if<StringValue>(&suffixArg->data)) {
        if (auto* str = std::get_if<StringValue>(&strArg->data)) {
          if (str->value.length() >= suffix->value.length()) {
            bool res = (0 == str->value.compare(str->value.length() - suffix->value.length(),
                                                suffix->value.length(),
                                                suffix->value));
            return std::make_shared<Value>(Value{BoolValue{res}});
          }
          return std::make_shared<Value>(Value{BoolValue{false}});
        }
      }
      throw std::runtime_error("endsWith expects two Strings");
    }}});
  }}});

  // contains: String -> String -> Bool
  globalEnv_["contains"] = std::make_shared<Value>(
      Value{FunctionValue{[](ValuePtr needleArg) -> ValuePtr {
        return std::make_shared<Value>(
            Value{FunctionValue{[needleArg](ValuePtr haystackArg) -> ValuePtr {
              if (auto* needle = std::get_if<StringValue>(&needleArg->data)) {
                if (auto* haystack = std::get_if<StringValue>(&haystackArg->data)) {
                  bool res = haystack->value.find(needle->value) != std::string::npos;
                  return std::make_shared<Value>(Value{BoolValue{res}});
                }
              }
              throw std::runtime_error("contains expects two Strings");
            }}});
      }}});

  // split: String -> String -> [String]
  globalEnv_["split"] = std::make_shared<Value>(
      Value{FunctionValue{[](ValuePtr delimArg) -> ValuePtr {
        return std::make_shared<Value>(Value{FunctionValue{[delimArg](ValuePtr strArg) -> ValuePtr {
          if (auto* delim = std::get_if<StringValue>(&delimArg->data)) {
            if (auto* str = std::get_if<StringValue>(&strArg->data)) {
              std::vector<ValuePtr> parts;
              std::string s = str->value;
              std::string delimiter = delim->value;
              size_t pos = 0;
              std::string token;
              if (delimiter.empty()) {
                // Split by character if delimiter is empty
                for (char c : s) {
                  parts.push_back(std::make_shared<Value>(Value{StringValue{std::string(1, c)}}));
                }
              } else {
                while ((pos = s.find(delimiter)) != std::string::npos) {
                  token = s.substr(0, pos);
                  parts.push_back(std::make_shared<Value>(Value{StringValue{token}}));
                  s.erase(0, pos + delimiter.length());
                }
                parts.push_back(std::make_shared<Value>(Value{StringValue{s}}));
              }
              return std::make_shared<Value>(Value{ListValue{parts}});
            }
          }
          throw std::runtime_error("split expects two Strings (delimiter and target)");
        }}});
      }}});

  // File I/O primitives

  // readFile :: String -> IO String
  globalEnv_["readFile"] = std::make_shared<Value>(
      Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
        if (auto* s = std::get_if<StringValue>(&arg->data)) {
          std::ifstream file(s->value);
          if (!file.is_open()) {
            throw std::runtime_error("readFile: cannot open file '" + s->value + "'");
          }
          std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
          file.close();
          return std::make_shared<Value>(Value{StringValue{content}});
        }
        throw std::runtime_error("readFile expects a String");
      }}});

  // writeFile :: String -> String -> IO ()
  globalEnv_["writeFile"] = std::make_shared<Value>(
      Value{FunctionValue{[](ValuePtr pathArg) -> ValuePtr {
        return std::make_shared<Value>(
            Value{FunctionValue{[pathArg](ValuePtr contentArg) -> ValuePtr {
              if (auto* path = std::get_if<StringValue>(&pathArg->data)) {
                if (auto* content = std::get_if<StringValue>(&contentArg->data)) {
                  std::ofstream file(path->value);
                  if (!file.is_open()) {
                    throw std::runtime_error("writeFile: cannot open file '" + path->value + "'");
                  }
                  file << content->value;
                  file.close();
                  return std::make_shared<Value>(Value{BoolValue{true}});
                }
              }
              throw std::runtime_error("writeFile expects two Strings");
            }}});
      }}});

  // appendFile :: String -> String -> IO ()
  globalEnv_["appendFile"] = std::make_shared<Value>(
      Value{FunctionValue{[](ValuePtr pathArg) -> ValuePtr {
        return std::make_shared<Value>(
            Value{FunctionValue{[pathArg](ValuePtr contentArg) -> ValuePtr {
              if (auto* path = std::get_if<StringValue>(&pathArg->data)) {
                if (auto* content = std::get_if<StringValue>(&contentArg->data)) {
                  std::ofstream file(path->value, std::ios::app);
                  if (!file.is_open()) {
                    throw std::runtime_error("appendFile: cannot open file '" + path->value + "'");
                  }
                  file << content->value;
                  file.close();
                  return std::make_shared<Value>(Value{BoolValue{true}});
                }
              }
              throw std::runtime_error("appendFile expects two Strings");
            }}});
      }}});

  // fileExists :: String -> IO Bool
  globalEnv_["fileExists"] = std::make_shared<Value>(
      Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
        if (auto* s = std::get_if<StringValue>(&arg->data)) {
          bool exists = std::filesystem::exists(s->value);
          return std::make_shared<Value>(Value{BoolValue{exists}});
        }
        throw std::runtime_error("fileExists expects a String");
      }}});

  // deleteFile :: String -> IO ()
  globalEnv_["deleteFile"] = std::make_shared<Value>(
      Value{FunctionValue{[](ValuePtr arg) -> ValuePtr {
        if (auto* s = std::get_if<StringValue>(&arg->data)) {
          if (std::filesystem::exists(s->value)) {
            std::filesystem::remove(s->value);
          }
          return std::make_shared<Value>(Value{BoolValue{true}});
        }
        throw std::runtime_error("deleteFile expects a String");
      }}});
}

// Module system integration methods
void Interpreter::setModuleResolver(std::shared_ptr<ModuleResolver> resolver) {
  moduleResolver_ = std::move(resolver);
}

void Interpreter::setNamespaceManager(std::shared_ptr<NamespaceManager> nsManager) {
  namespaceManager_ = std::move(nsManager);
}

ValuePtr Interpreter::force(const ValuePtr& value) {
  if (auto* thunk = std::get_if<ThunkValue>(&value->data)) {
    return thunk->force();
  }
  return value;
}

ValuePtr Interpreter::eval(const Expr& expr) {
  return eval(expr, globalEnv_);
}

ValuePtr Interpreter::eval(const Expr& expr, Environment& env) {
  return std::visit(
      overload{
          [&](const Var& v) -> ValuePtr {
            // For qualified names, always use namespace manager
            size_t dotPos = v.name.find('.');
            if (dotPos != std::string::npos && namespaceManager_) {
              std::string qualifier = v.name.substr(0, dotPos);
              std::string symbolName = v.name.substr(dotPos + 1);

              auto result = namespaceManager_->lookupQualified(qualifier, symbolName);
              if (result && result->value) {
                return force(result->value);
              }

              throw std::runtime_error("Qualified name '" + v.name + "' not found\n" +
                                       "  Module '" + qualifier +
                                       "' is not imported or doesn't export '" + symbolName +
                                       "'\n" + "  Hint: Check your import declarations");
            }

            // For unqualified names: check environment first (includes prelude & local defs)
            auto it = env.find(v.name);
            if (it != env.end()) {
              return force(it->second);
            }

            // Then check namespace manager (for imported symbols)
            if (namespaceManager_) {
              auto result = namespaceManager_->lookup(v.name);
              if (result && result->value) {
                // Check for ambiguity
                if (namespaceManager_->isAmbiguous(v.name)) {
                  auto modules = namespaceManager_->getModulesExporting(v.name);
                  std::string msg = "Ambiguous symbol '" + v.name + "' imported from:\n";
                  for (const auto& mod : modules) {
                    msg += "  - " + mod + "\n";
                  }
                  msg += "Hint: Use qualified import or specify module: ";
                  for (size_t i = 0; i < modules.size(); ++i) {
                    if (i > 0)
                      msg += ", ";
                    msg += modules[i] + "." + v.name;
                  }
                  throw std::runtime_error(msg);
                }
                return force(result->value);
              }
            }

            // Symbol not found anywhere
            std::string error = "Undefined variable: " + v.name;

            // DEBUG: Print available keys
            std::cout << "Available variables in env: ";
            for (const auto& [key, _] : env) {
              std::cout << key << " ";
            }
            std::cout << std::endl;


            // Smart suggestion: find modules that export this symbol
            if (namespaceManager_) {
              auto suggestions = namespaceManager_->suggestImportsFor(v.name);
              if (!suggestions.empty()) {
                error += "\n\n Hint: Symbol '" + v.name + "' is available in:\n";
                for (const auto& moduleName : suggestions) {
                  error += "  • " + moduleName + "\n";
                }
                error += "\nSuggested fix:\n";
                if (suggestions.size() == 1) {
                  error += "  import " + suggestions[0] + "\n";
                  error += "  -- or --\n";
                  error += "  import " + suggestions[0] + " (" + v.name + ")";
                } else {
                  error += "  Choose one:\n";
                  for (const auto& moduleName : suggestions) {
                    error += "    import " + moduleName + " (" + v.name + ")\n";
                  }
                }
              }
            } else {
            }

            throw std::runtime_error(error);
          },
          [&](const Lit& l) -> ValuePtr {
            return std::visit(overload{[](int64_t i) -> ValuePtr {
                                         return std::make_shared<Value>(Value{IntValue{i}});
                                       },
                                       [](double d) -> ValuePtr {
                                         return std::make_shared<Value>(Value{FloatValue{d}});
                                       },
                                       [](const std::string& s) -> ValuePtr {
                                         return std::make_shared<Value>(Value{StringValue{s}});
                                       },
                                       [](bool b) -> ValuePtr {
                                         return std::make_shared<Value>(Value{BoolValue{b}});
                                       },
                                       [](const BigInt& bi) -> ValuePtr {
                                         return std::make_shared<Value>(Value{BigIntValue{bi}});
                                       }},
                              l.value);
          },
          [&](const FunctionDecl& func) -> ValuePtr {
            // std::cout << "Evaluating FunctionDecl: " << func.name << " params: " <<
            // func.params.size() << std::endl;

            // Capture the current environment for the closure
            Environment capturedEnv = env;
            return std::make_shared<Value>(Value{FunctionValue{[lam = &func,
                                                                capturedEnv](ValuePtr arg) mutable
                                                                   -> ValuePtr {
              Interpreter interp;
              // Match first parameter pattern and bind
              if (!interp.matchPattern(*lam->params[0], arg, capturedEnv)) {
                throw std::runtime_error("Pattern match failed");
              }

              // If more params, return a curried lambda
              if (lam->params.size() > 1) {
                const FunctionDecl* lamPtr = lam;  // Keep pointer to original lambda
                size_t nextParamIndex = 1;
                size_t totalParams = lam->params.size();

                auto newCapturedEnv = capturedEnv;

                // Create curried function for remaining params
                std::function<ValuePtr(ValuePtr)> curriedFunc;
                curriedFunc = [lamPtr, nextParamIndex, totalParams, newCapturedEnv, curriedFunc](
                                  ValuePtr nextArg) mutable -> ValuePtr {
                  Interpreter innerInterp;

                  // Match current parameter
                  if (!innerInterp.matchPattern(*lamPtr->params[nextParamIndex],
                                                nextArg,
                                                newCapturedEnv)) {
                    throw std::runtime_error("Pattern match failed");
                  }

                  // If this was the last param, evaluate body
                  if (nextParamIndex + 1 >= totalParams) {
                    return innerInterp.eval(*lamPtr->body, newCapturedEnv);
                  }

                  // Otherwise, return another curried function for next param
                  size_t nextIdx = nextParamIndex + 1;
                  auto furtherEnv = newCapturedEnv;

                  // Simpler approach: explicitly construct next lambda::function.

                  return std::make_shared<Value>(Value{FunctionValue{
                      [lamPtr, nextIdx, totalParams, furtherEnv](ValuePtr arg) mutable -> ValuePtr {
                        Interpreter deepInterp;

                        if (!deepInterp.matchPattern(*lamPtr->params[nextIdx], arg, furtherEnv)) {
                          throw std::runtime_error("Pattern match failed");
                        }

                        if (nextIdx + 1 >= totalParams) {
                          return deepInterp.eval(*lamPtr->body, furtherEnv);
                        }


                        // Recursively curry for remaining parameters
                        // Each level captures the environment with bound parameters
                        std::function<ValuePtr(ValuePtr)> buildCurriedFunction;
                        buildCurriedFunction =
                            [lamPtr, nextIdx, totalParams, furtherEnv, &buildCurriedFunction](
                                ValuePtr arg) mutable -> ValuePtr {
                          Interpreter deepInterp;
                          auto deepEnv = furtherEnv;

                          if (!deepInterp.matchPattern(*lamPtr->params[nextIdx], arg, deepEnv)) {
                            throw std::runtime_error("Pattern match failed");
                          }

                          if (nextIdx + 1 >= totalParams) {
                            return deepInterp.eval(*lamPtr->body, deepEnv);
                          }

                          // Return another curried function for next parameter
                          return std::make_shared<Value>(Value{FunctionValue{
                              [lamPtr, nextIdx_cached = nextIdx + 1, totalParams, deepEnv](
                                  ValuePtr nextArg) mutable -> ValuePtr {
                                Interpreter nextInterp;
                                if (!nextInterp.matchPattern(*lamPtr->params[nextIdx_cached],
                                                             nextArg,
                                                             deepEnv)) {
                                  throw std::runtime_error("Pattern match failed");
                                }
                                if (nextIdx_cached + 1 >= totalParams) {
                                  return nextInterp.eval(*lamPtr->body, deepEnv);
                                }
                                // Recursively call buildCurriedFunction for the next parameter
                                std::function<ValuePtr(ValuePtr)> nextBuildCurriedFunction;
                                nextBuildCurriedFunction =
                                    [lamPtr,
                                     nextIdx_cached,
                                     totalParams,
                                     deepEnv,
                                     &nextBuildCurriedFunction](ValuePtr arg) mutable -> ValuePtr {
                                  Interpreter innerInterp;
                                  auto innerEnv = deepEnv;
                                  if (!innerInterp.matchPattern(*lamPtr->params[nextIdx_cached],
                                                                arg,
                                                                innerEnv)) {
                                    throw std::runtime_error("Pattern match failed");
                                  }
                                  if (nextIdx_cached + 1 >= totalParams) {
                                    return innerInterp.eval(*lamPtr->body, innerEnv);
                                  }
                                  return std::make_shared<Value>(Value{FunctionValue{
                                      [lamPtr,
                                       nextIdx_cached_inner = nextIdx_cached + 1,
                                       totalParams,
                                       innerEnv](ValuePtr nextArg) mutable -> ValuePtr {
                                        Interpreter finalInterp;
                                        if (!finalInterp.matchPattern(
                                                *lamPtr->params[nextIdx_cached_inner],
                                                nextArg,
                                                innerEnv)) {
                                          throw std::runtime_error("Pattern match failed");
                                        }
                                        return finalInterp.eval(*lamPtr->body, innerEnv);
                                      }}});
                                };
                                return std::make_shared<Value>(
                                    Value{FunctionValue{nextBuildCurriedFunction}});
                              }}});
                        };

                        return std::make_shared<Value>(Value{FunctionValue{buildCurriedFunction}});
                      }}});
                };

                return std::make_shared<Value>(Value{FunctionValue{curriedFunc}});
              }

              return interp.eval(*lam->body, capturedEnv);
            }}});
          },
          [&](const Lambda& lam) -> ValuePtr {
            // Capture environment
            auto capturedEnv = env;
            return std::make_shared<Value>(
                Value{FunctionValue{[lam = &lam, capturedEnv](ValuePtr arg) mutable -> ValuePtr {
                  Interpreter interp;
                  // Match first parameter pattern and bind
                  if (!interp.matchPattern(*lam->params[0], arg, capturedEnv)) {
                    throw std::runtime_error("Pattern match failed");
                  }


                  // Handle multi-parameter lambdas with proper currying
                  if (lam->params.size() > 1) {
                    // Return curried function for remaining parameters
                    std::function<ValuePtr(size_t, Environment)> buildLambdaCurry;
                    buildLambdaCurry = [lam, &buildLambdaCurry](size_t paramIdx,
                                                                Environment env) -> ValuePtr {
                      if (paramIdx >= lam->params.size()) {
                        Interpreter interp;
                        return interp.eval(*lam->body, env);
                      }

                      return std::make_shared<Value>(
                          Value{FunctionValue{[lam, paramIdx, env, &buildLambdaCurry](
                                                  ValuePtr arg) mutable -> ValuePtr {
                            Interpreter interp;
                            if (!interp.matchPattern(*lam->params[paramIdx], arg, env)) {
                              throw std::runtime_error("Pattern match failed in lambda");
                            }
                            return buildLambdaCurry(paramIdx + 1, env);
                          }}});
                    };

                    return buildLambdaCurry(1, capturedEnv);
                  }

                  return interp.eval(*lam->body, capturedEnv);
                }}});
          },
          [&](const App& app) -> ValuePtr {
            ValuePtr func = force(eval(*app.func, env));
            ValuePtr arg = eval(*app.arg, env);  // Lazy: don't force arg yet

            if (auto* funcVal = std::get_if<FunctionValue>(&func->data)) {
              return funcVal->func(arg);
            }
            if (auto* constrVal = std::get_if<ConstructorValue>(&func->data)) {
              std::vector<ValuePtr> args = constrVal->args;
              args.push_back(arg);
              return std::make_shared<Value>(Value{ConstructorValue{constrVal->name, args}});
            }
            // std::cerr << "Failed to apply: " << valueToString(func) << std::endl;
            throw std::runtime_error("Cannot apply non-function");
          },
          [&](const Let& let) -> ValuePtr {
            // Recursive binding support for variable patterns
            std::string name;
            if (auto* varPat = std::get_if<VarPat>(&let.pattern->node)) {
              name = varPat->name;
            }

            if (!name.empty()) {
              // 1. Create a placeholder value
              ValuePtr placeholder = std::make_shared<Value>();

              // 2. Create new env with placeholder
              Environment newEnv = env;
              newEnv[name] = placeholder;

              // 3. Evaluate value in newEnv (so it captures the placeholder)
              ValuePtr val = eval(*let.value, newEnv);

              // 4. Update placeholder content with the result
              placeholder->data = val->data;

              // 5. Evaluate body in newEnv
              return eval(*let.body, newEnv);
            }

            // Evaluate the value expression in the current environment
            ValuePtr value = eval(*let.value, env);

            // Create new environment extending the current one (not starting fresh!)
            // Critical: use newEnv for nested lets to work correctly
            Environment newEnv = env;  // Start with current env, not empty!

            if (!matchPattern(*let.pattern, value, newEnv)) {
              throw std::runtime_error("Pattern match failed in let");
            }
            // std::cout << "Bound let pattern. Env size: " << newEnv.size() << std::endl;
            return eval(*let.body, newEnv);
          },
          [&](const Match& m) -> ValuePtr {
            ValuePtr scrutinee = force(eval(*m.scrutinee, env));

            for (const auto& [pattern, expr] : m.arms) {
              Environment newEnv = env;
              if (matchPattern(*pattern, scrutinee, newEnv)) {
                return eval(*expr, newEnv);
              }
            }
            throw std::runtime_error("Non-exhaustive pattern match");
          },
          [&](const If& ifExpr) -> ValuePtr {
            ValuePtr cond = force(eval(*ifExpr.cond, env));
            if (auto* b = std::get_if<BoolValue>(&cond->data)) {
              if (b->value) {
                return eval(*ifExpr.thenBranch, env);
              } else {
                return eval(*ifExpr.elseBranch, env);
              }
            }
            throw std::runtime_error("If condition must be boolean");
          },
          [&](const BinOp& op) -> ValuePtr {
            ValuePtr left = force(eval(*op.left, env));
            ValuePtr right = force(eval(*op.right, env));
            return evalBinOp(op.op, left, right);
          },
          [&](const List& list) -> ValuePtr {
            std::vector<ValuePtr> elements;
            for (const auto& elem : list.elements) {
              elements.push_back(eval(*elem, env));
            }
            return std::make_shared<Value>(Value{ListValue{elements}});
          },
          [&](const Record& rec) -> ValuePtr {
            std::map<std::string, ValuePtr> fields;
            for (const auto& [name, expr] : rec.fields) {
              fields[name] = eval(*expr, env);
            }
            return std::make_shared<Value>(Value{RecordValue{fields}});
          },
          [&](const RecordAccess& access) -> ValuePtr {
            ValuePtr record = eval(*access.record, env);
            ValuePtr forced = force(record);
            if (auto* rec = std::get_if<RecordValue>(&forced->data)) {
              auto it = rec->fields.find(access.field);
              if (it != rec->fields.end()) {
                return force(it->second);
              }
              throw std::runtime_error("Field not found: " + access.field);
            }
            throw std::runtime_error("Not a record");
          },
          [&](const RecordUpdate& update) -> ValuePtr {
            // Evaluate the base record
            ValuePtr baseRecord = eval(*update.record, env);
            ValuePtr forced = force(baseRecord);

            if (auto* rec = std::get_if<RecordValue>(&forced->data)) {
              // Start with a copy of all fields from the base record
              std::map<std::string, ValuePtr> newFields = rec->fields;

              // Update with new values
              for (const auto& [name, expr] : update.updates) {
                newFields[name] = force(eval(*expr, env));
              }

              return std::make_shared<Value>(Value{RecordValue{newFields}});
            }
            throw std::runtime_error("Cannot update non-record");
          },
          [&](const Block& block) -> ValuePtr {
            // Block evaluation: thread environment through statements
            // Let bindings accumulate in blockEnv for subsequent statements
            // Example: do { let x = 5; let y = 10; print (x + y) }
            // Parser creates: Block [ Let{x, 5, print ...}, Let{y, 10, print ...} ]
            // Each Let adds binding to blockEnv, making it visible to later statements

            ValuePtr result = std::make_shared<Value>(Value{BoolValue{true}});
            Environment blockEnv = env;

            for (const auto& stmt : block.stmts) {
              // Check if this statement is a Let expression
              if (auto* let = std::get_if<Let>(&stmt->node)) {
                // Evaluate the value in the current block environment
                ValuePtr value = eval(*let->value, blockEnv);

                // Add the binding to the block environment
                if (!matchPattern(*let->pattern, value, blockEnv)) {
                  throw std::runtime_error("Pattern match failed in let binding");
                }

                // Evaluate the body with the updated block environment
                result = eval(*let->body, blockEnv);
              } else {
                // Not a Let, just evaluate normally
                result = eval(*stmt, blockEnv);
              }
            }

            return result;
          },
          [&](const Bind& bind) -> ValuePtr {
            // Evaluate the action
            ValuePtr value = eval(*bind.value, env);

            // Match the result against the pattern
            Environment newEnv = env;
            if (!matchPattern(*bind.pattern, value, newEnv)) {
              throw std::runtime_error("Pattern match failed in bind");
            }

            // Evaluate the body with the bound variable
            return eval(*bind.body, newEnv);
          },
          [&](const Strict& s) -> ValuePtr {
            return force(eval(*s.expr, env));
          }},
      expr.node);
}

void Interpreter::evalDeclAtIndex(size_t declIndex) {
  const Decl& decl = *declarations_[declIndex];
  std::visit(
      overload{
          [&, declIndex](const FunctionDecl& func) {
            // Check if placeholder already exists from two-pass evaluation
            ValuePtr placeholder;
            auto it = globalEnv_.find(func.name);
            if (it != globalEnv_.end()) {
              placeholder = it->second;
            } else {
              placeholder = std::make_shared<Value>();
              globalEnv_[func.name] = placeholder;
            }

            // Function closures must see the live global environment
            // to support forward references and mutual recursion
            struct CurriedInvoker {
              const FunctionDecl* func;
              size_t index;
              Interpreter* interp;
              Environment capturedEnv;  // Capture environment with previous bindings

              ValuePtr operator()(ValuePtr arg) {
                // Start with captured environment
                Environment callEnv = capturedEnv;

                // Bind current argument
                if (!interp->matchPattern(*func->params[index], arg, callEnv)) {
                  throw std::runtime_error("Pattern match failed in function argument");
                }

                if (index + 1 >= func->params.size()) {
                  // If this is the last argument, evaluate the body
                  // Use accumulated callEnv with all args bound
                  return interp->eval(*func->body, callEnv);
                }

                // Return next curried function, capturing the new environment
                return std::make_shared<Value>(
                    Value{FunctionValue{CurriedInvoker{func, index + 1, interp, callEnv}}});
              }
            };


            // Wrap ALL functions in thunks for forward references
            // Zero-param functions are lazy-evaluated thunks
            ValuePtr funcVal;
            if (func.params.empty()) {
              // Zero-param functions become thunks for forward references
              // Capture the declaration index (passed as lambda capture)
              funcVal = std::make_shared<Value>(Value{ThunkValue{[this, declIndex]() {
                // Access function from stable storage by index
                const auto& storedDecl = declarations_[declIndex];
                const auto& storedFunc = std::get<FunctionDecl>(storedDecl->node);
                return eval(*storedFunc.body, globalEnv_);
              }}});
            } else {
              funcVal = std::make_shared<Value>(
                  Value{FunctionValue{CurriedInvoker{&func, 0, this, globalEnv_}}});
            }

            // Update placeholder with actual function value
            placeholder->data = funcVal->data;
          },
          [&](const TypeDecl& typeDecl) {
            // Handle ADT constructors
            if (auto* adt = std::get_if<std::vector<std::pair<std::string, std::vector<TypePtr>>>>(
                    &typeDecl.rhs)) {
              for (const auto& [ctorName, args] : *adt) {
                if (args.empty()) {
                  // 0-arity constructor: just a value
                  globalEnv_[ctorName] = std::make_shared<Value>(
                      Value{ConstructorValue{ctorName, {}}});
                } else {
                  // N-arity constructor: curried function
                  struct ConstructorBuilder {
                    std::string name;
                    size_t totalArgs;
                    size_t currentArg;
                    std::vector<ValuePtr> collectedArgs;

                    ValuePtr operator()(ValuePtr arg) {
                      auto newArgs = collectedArgs;
                      newArgs.push_back(arg);

                      if (currentArg + 1 >= totalArgs) {
                        return std::make_shared<Value>(Value{ConstructorValue{name, newArgs}});
                      }

                      return std::make_shared<Value>(Value{FunctionValue{
                          ConstructorBuilder{name, totalArgs, currentArg + 1, newArgs}}});
                    }
                  };

                  globalEnv_[ctorName] = std::make_shared<Value>(
                      Value{FunctionValue{ConstructorBuilder{ctorName, args.size(), 0, {}}}});
                }
              }
            }
          },
          [&](const ModuleDecl&) {},
          [&](const ImportDecl& import) {
            // Use ModuleResolver if available, otherwise fall back to legacy logic
            if (!moduleResolver_) {
              std::cerr << "Warning: ModuleResolver not set, using legacy import logic"
                        << std::endl;

              // Legacy fallback: basic module loading
              if (loadedModules_.count(import.moduleName)) {
                return;
              }
              loadedModules_.insert(import.moduleName);

              std::string path = import.moduleName;
              std::replace(path.begin(), path.end(), '.', '/');
              path += ".solis";

              std::ifstream file(path);
              if (!file.is_open()) {
                std::cerr << "Warning: Could not find module " << import.moduleName << " at "
                          << path << std::endl;
                return;
              }

              std::string source((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

              Lexer lexer(source);
              Parser parser(lexer.tokenize());
              try {
                auto module = parser.parseModule();

                for (auto& decl : module.declarations) {
                  eval(*decl);
                  declarations_.push_back(std::move(decl));
                }
              } catch (const std::exception& e) {
                std::cerr << "Error loading module " << import.moduleName << ": " << e.what()
                          << std::endl;
              }
              return;
            }

            // Modern path: use ModuleResolver
            if (moduleResolver_->isLoaded(import.moduleName)) {
              return;  // Already loaded
            }

            auto modulePath = moduleResolver_->resolveModule(import.moduleName, currentDirectory_);

            if (!modulePath) {
              std::cerr << "Error: Module not found: " << import.moduleName << std::endl;
              std::cerr << "Search paths:" << std::endl;
              for (const auto& path : moduleResolver_->getSearchPaths(currentDirectory_)) {
                std::cerr << "  - " << path << std::endl;
              }
              throw std::runtime_error("Module not found: " + import.moduleName);
            }

            moduleResolver_->markLoaded(import.moduleName);

            // Read and parse module
            std::ifstream file(*modulePath);
            if (!file.is_open()) {
              throw std::runtime_error("Could not open module file: " + *modulePath);
            }

            std::string source((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

            Lexer lexer(source);
            Parser parser(lexer.tokenize());
            auto module = parser.parseModule();

            // Update current directory for nested imports
            std::string prevDir = currentDirectory_;
            currentDirectory_ = std::filesystem::path(*modulePath).parent_path().string();

            // Process nested imports recursively
            for (const auto& subImport : module.imports) {
              Decl importDecl;
              importDecl.node = subImport;
              eval(importDecl);
            }

            // Collect exported symbols
            std::vector<NamespaceManager::Symbol> exportedSymbols;

            for (auto& decl : module.declarations) {
              // Evaluate declaration to populate global environment
              eval(*decl);

              // Extract function name for export
              if (auto* funcDecl = std::get_if<FunctionDecl>(&decl->node)) {
                bool isExported = true;

                // Check if module has export list
                if (module.moduleDecl && !module.moduleDecl->exports.empty()) {
                  auto& exports = module.moduleDecl->exports;
                  isExported = std::find(exports.begin(), exports.end(), funcDecl->name) !=
                               exports.end();
                }

                if (isExported && globalEnv_.count(funcDecl->name)) {
                  NamespaceManager::Symbol symbol;
                  symbol.name = funcDecl->name;
                  symbol.moduleName = import.moduleName;
                  symbol.value = globalEnv_[funcDecl->name];
                  symbol.isExported = true;
                  exportedSymbols.push_back(symbol);
                }
              }

              declarations_.push_back(std::move(decl));
            }

            // Register import with namespace manager
            if (namespaceManager_) {
              // Register module in catalog for smart import suggestions
              // namespaceManager_->registerModuleCatalog(import.moduleName, exportedSymbols);

              // Register the import (adds to qualified/unqualified namespaces)
              namespaceManager_->addImport(import, exportedSymbols);
            }

            // Restore previous directory
            currentDirectory_ = prevDir;
          },
          [&](const TraitDecl&) {},
          [&](const ImplDecl&) {
          }},
      decl.node);
}

void Interpreter::evalAndStore(DeclPtr decl) {
  // Store declaration FIRST to keep it alive (thunks will reference it by index)
  size_t declIndex = declarations_.size();
  declarations_.push_back(std::move(decl));
  // Then evaluate using the stored declaration, passing the index
  evalDeclAtIndex(declIndex);
}

void Interpreter::eval(const Decl& decl) {
  // Fallback for direct evaluation (used in legacy import code)
  // This is less efficient but maintains compatibility
  // Just evaluate directly without the index optimization
  std::visit(
      overload{[&](const FunctionDecl& func) {
                 ValuePtr placeholder;
                 auto it = globalEnv_.find(func.name);
                 if (it != globalEnv_.end()) {
                   placeholder = it->second;
                 } else {
                   placeholder = std::make_shared<Value>();
                   globalEnv_[func.name] = placeholder;
                 }

                 struct CurriedInvoker {
                   const FunctionDecl* func;
                   size_t index;
                   Interpreter* interp;
                   Environment capturedEnv;

                   ValuePtr operator()(ValuePtr arg) {
                     Environment callEnv = capturedEnv;
                     if (!interp->matchPattern(*func->params[index], arg, callEnv)) {
                       throw std::runtime_error("Pattern match failed in function argument");
                     }
                     if (index + 1 >= func->params.size()) {
                       return interp->eval(*func->body, callEnv);
                     }
                     return std::make_shared<Value>(
                         Value{FunctionValue{CurriedInvoker{func, index + 1, interp, callEnv}}});
                   }
                 };

                 ValuePtr funcVal;
                 if (func.params.empty()) {
                   // Create thunk that captures func by pointer (may be unsafe if not stored)
                   funcVal = std::make_shared<Value>(Value{ThunkValue{[this, &func]() {
                     return eval(*func.body, globalEnv_);
                   }}});
                 } else {
                   funcVal = std::make_shared<Value>(
                       Value{FunctionValue{CurriedInvoker{&func, 0, this, globalEnv_}}});
                 }
                 placeholder->data = funcVal->data;
               },
               [&](const TypeDecl& typeDecl) {
                 if (auto* adt =
                         std::get_if<std::vector<std::pair<std::string, std::vector<TypePtr>>>>(
                             &typeDecl.rhs)) {
                   for (const auto& [ctorName, args] : *adt) {
                     if (args.empty()) {
                       globalEnv_[ctorName] = std::make_shared<Value>(
                           Value{ConstructorValue{ctorName, {}}});
                     } else {
                       struct ConstructorBuilder {
                         std::string name;
                         size_t totalArgs;
                         size_t currentArg;
                         std::vector<ValuePtr> collectedArgs;
                         ValuePtr operator()(ValuePtr arg) {
                           auto newArgs = collectedArgs;
                           newArgs.push_back(arg);
                           if (currentArg + 1 >= totalArgs) {
                             return std::make_shared<Value>(Value{ConstructorValue{name, newArgs}});
                           }
                           return std::make_shared<Value>(Value{FunctionValue{
                               ConstructorBuilder{name, totalArgs, currentArg + 1, newArgs}}});
                         }
                       };
                       globalEnv_[ctorName] = std::make_shared<Value>(
                           Value{FunctionValue{ConstructorBuilder{ctorName, args.size(), 0, {}}}});
                     }
                   }
                 }
               },
               [&](const ModuleDecl&) {},
               [&](const ImportDecl&) {},
               [&](const TraitDecl&) {},
               [&](const ImplDecl&) {
               }},
      decl.node);
}

ValuePtr Interpreter::evalBinOp(const std::string& op,
                                const ValuePtr& left,
                                const ValuePtr& right) {
  if (op == "+") {
    // Int + Int
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{IntValue{li->value + ri->value}});
      }
      // Int + BigInt =>BigInt
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        BigInt result = BigInt(li->value) + rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
    // BigInt + Int =>BigInt
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        BigInt result = lb->value + BigInt(ri->value);
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
      // BigInt + BigInt
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        BigInt result = lb->value + rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
    // Float + Float
    if (auto* lf = std::get_if<FloatValue>(&left->data)) {
      if (auto* rf = std::get_if<FloatValue>(&right->data)) {
        return std::make_shared<Value>(Value{FloatValue{lf->value + rf->value}});
      }
    }
  } else if (op == "-") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{IntValue{li->value - ri->value}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        BigInt result = BigInt(li->value) - rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        BigInt result = lb->value - BigInt(ri->value);
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        BigInt result = lb->value - rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
  } else if (op == "*") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{IntValue{li->value * ri->value}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        BigInt result = BigInt(li->value) * rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        BigInt result = lb->value * BigInt(ri->value);
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        BigInt result = lb->value * rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
  } else if (op == "/") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        if (ri->value == 0)
          throw std::runtime_error("Division by zero");
        return std::make_shared<Value>(Value{IntValue{li->value / ri->value}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        if (rb->value == BigInt(0))
          throw std::runtime_error("Division by zero");
        BigInt result = BigInt(li->value) / rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        if (ri->value == 0)
          throw std::runtime_error("Division by zero");
        BigInt result = lb->value / BigInt(ri->value);
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        if (rb->value == BigInt(0))
          throw std::runtime_error("Division by zero");
        BigInt result = lb->value / rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
  } else if (op == "%") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        if (ri->value == 0)
          throw std::runtime_error("Modulo by zero");
        return std::make_shared<Value>(Value{IntValue{li->value % ri->value}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        if (rb->value == BigInt(0))
          throw std::runtime_error("Modulo by zero");
        BigInt result = BigInt(li->value) % rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        if (ri->value == 0)
          throw std::runtime_error("Modulo by zero");
        BigInt result = lb->value % BigInt(ri->value);
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        if (rb->value == BigInt(0))
          throw std::runtime_error("Modulo by zero");
        BigInt result = lb->value % rb->value;
        return std::make_shared<Value>(Value{BigIntValue{result}});
      }
    }
  } else if (op == "==") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{li->value == ri->value}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{BigInt(li->value) == rb->value}});
      }
    }
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value == BigInt(ri->value)}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value == rb->value}});
      }
    }
    if (auto* ls = std::get_if<StringValue>(&left->data)) {
      if (auto* rs = std::get_if<StringValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{ls->value == rs->value}});
      }
    } else if (auto* lb = std::get_if<BoolValue>(&left->data)) {
      if (auto* rb = std::get_if<BoolValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value == rb->value}});
      }
    }
  } else if (op == "!=") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{li->value != ri->value}});
      }
    } else if (auto* ls = std::get_if<StringValue>(&left->data)) {
      if (auto* rs = std::get_if<StringValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{ls->value != rs->value}});
      }
    } else if (auto* lb = std::get_if<BoolValue>(&left->data)) {
      if (auto* rb = std::get_if<BoolValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value != rb->value}});
      }
    }
  } else if (op == "%") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        if (ri->value == 0)
          throw std::runtime_error("Modulo by zero");
        return std::make_shared<Value>(Value{IntValue{li->value % ri->value}});
      }
    }
  } else if (op == "<") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{li->value < ri->value}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{BigInt(li->value) < rb->value}});
      }
    }
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value < BigInt(ri->value)}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value < rb->value}});
      }
    }
  } else if (op == ">") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{li->value > ri->value}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{BigInt(li->value) > rb->value}});
      }
    }
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value > BigInt(ri->value)}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value > rb->value}});
      }
    }
  } else if (op == "<=") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{li->value <= ri->value}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{BigInt(li->value) <= rb->value}});
      }
    }
    if (auto* lb = std::get_if<BigIntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value <= BigInt(ri->value)}});
      }
      if (auto* rb = std::get_if<BigIntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value <= rb->value}});
      }
    }
  } else if (op == ">=") {
    if (auto* li = std::get_if<IntValue>(&left->data)) {
      if (auto* ri = std::get_if<IntValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{li->value >= ri->value}});
      }
    }
    if (auto* lf = std::get_if<FloatValue>(&left->data)) {
      if (auto* rf = std::get_if<FloatValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lf->value >= rf->value}});
      }
    }
  } else if (op == "++") {
    if (auto* ls = std::get_if<StringValue>(&left->data)) {
      if (auto* rs = std::get_if<StringValue>(&right->data)) {
        return std::make_shared<Value>(Value{StringValue{ls->value + rs->value}});
      }
    }
    if (auto* ll = std::get_if<ListValue>(&left->data)) {
      if (auto* rl = std::get_if<ListValue>(&right->data)) {
        std::vector<ValuePtr> newElements = ll->elements;
        newElements.insert(newElements.end(), rl->elements.begin(), rl->elements.end());
        return std::make_shared<Value>(Value{ListValue{newElements}});
      }
    }
  } else if (op == ":") {
    // Cons operator: elem : list
    // Right side MUST be a list
    if (auto* rl = std::get_if<ListValue>(&right->data)) {
      std::vector<ValuePtr> newElements;
      newElements.push_back(left);
      newElements.insert(newElements.end(), rl->elements.begin(), rl->elements.end());
      return std::make_shared<Value>(Value{ListValue{newElements}});
    }
    throw std::runtime_error("Right side of ':' must be a list");
  } else if (op == "&&") {
    if (auto* lb = std::get_if<BoolValue>(&left->data)) {
      if (auto* rb = std::get_if<BoolValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value && rb->value}});
      }
    }
    throw std::runtime_error("Both operands of '&&' must be boolean");
  } else if (op == "||") {
    if (auto* lb = std::get_if<BoolValue>(&left->data)) {
      if (auto* rb = std::get_if<BoolValue>(&right->data)) {
        return std::make_shared<Value>(Value{BoolValue{lb->value || rb->value}});
      }
    }
    throw std::runtime_error("Both operands of '||' must be boolean");
  }

  throw std::runtime_error("Unsupported operator: " + op);
}

bool Interpreter::matchPattern(const Pattern& pattern, const ValuePtr& value, Environment& env) {
  ValuePtr forced = force(value);

  return std::visit(
      overload{
          [&](const VarPat& v) {
            env[v.name] = forced;
            return true;
          },
          [&](const LitPat& l) {
            return std::visit(overload{[&](int64_t i) {
                                         if (auto* iv = std::get_if<IntValue>(&forced->data)) {
                                           return iv->value == i;
                                         }
                                         return false;
                                       },
                                       [&](double d) {
                                         if (auto* fv = std::get_if<FloatValue>(&forced->data)) {
                                           return fv->value == d;
                                         }
                                         return false;
                                       },
                                       [&](const std::string& s) {
                                         if (auto* sv = std::get_if<StringValue>(&forced->data)) {
                                           return sv->value == s;
                                         }
                                         return false;
                                       },
                                       [&](bool b) {
                                         if (auto* bv = std::get_if<BoolValue>(&forced->data)) {
                                           return bv->value == b;
                                         }
                                         return false;
                                       },
                                       [&](const BigInt& bi) {
                                         if (auto* biv = std::get_if<BigIntValue>(&forced->data)) {
                                           return biv->value == bi;
                                         }
                                         return false;
                                       }},
                              l.value);
          },
          [&](const ConsPat& cp) -> bool {
            // First check if value is a ConstructorValue (runtime constructors like Just, Nothing)
            if (auto* cv = std::get_if<ConstructorValue>(&forced->data)) {
              // Check if constructor names match
              if (cv->name != cp.constructor) {
                return false;
              }

              // Match the number of arguments
              if (cv->args.size() != cp.args.size()) {
                return false;
              }

              // Match each argument pattern
              for (size_t i = 0; i < cp.args.size(); ++i) {
                if (!matchPattern(*cp.args[i], cv->args[i], env)) {
                  return false;
                }
              }
              return true;
            }

            // Handle cons pattern for lists: head:tail (constructor = "::")
            if (cp.constructor == "::") {
              if (auto* lv = std::get_if<ListValue>(&forced->data)) {
                if (lv->elements.empty() || cp.args.size() != 2) {
                  return false;
                }

                // Match head
                if (!matchPattern(*cp.args[0], lv->elements[0], env)) {
                  return false;
                }

                // Create tail list
                std::vector<ValuePtr> tailElements(lv->elements.begin() + 1, lv->elements.end());
                ValuePtr tailList = std::make_shared<Value>(Value{ListValue{tailElements}});

                // Match tail
                return matchPattern(*cp.args[1], tailList, env);
              }
            }

            return false;
          },
          [&](const ListPat& lp) -> bool {
            if (auto* lv = std::get_if<ListValue>(&forced->data)) {
              if (lv->elements.size() != lp.elements.size()) {
                return false;
              }
              for (size_t i = 0; i < lp.elements.size(); ++i) {
                if (!matchPattern(*lp.elements[i], lv->elements[i], env)) {
                  return false;
                }
              }
              return true;
            }
            if (lp.elements.empty()) {
              // Empty pattern matches empty list
              return std::get_if<ListValue>(&forced->data) != nullptr;
            }
            return false;
          },
          [&](const RecordPat& rp) -> bool {
            if (auto* rv = std::get_if<RecordValue>(&forced->data)) {
              // All fields in pattern must match
              for (const auto& [field, pattern] : rp.fields) {
                auto it = rv->fields.find(field);
                if (it == rv->fields.end() || !matchPattern(*pattern, it->second, env)) {
                  return false;
                }
              }
              return true;
            }
            return false;
          },
          [&](const WildcardPat&) -> bool {
            return true;
          }},
      pattern.node);
}

void Interpreter::addBinding(const std::string& name, ValuePtr value) {
  globalEnv_[name] = value;
}

std::string Interpreter::valueToString(const ValuePtr& value) {
  ValuePtr forced = force(value);
  return std::visit(overload{[](const IntValue& i) { return std::to_string(i.value); },
                             [](const FloatValue& f) { return std::to_string(f.value); },
                             [](const StringValue& s) { return s.value; },
                             [](const BoolValue& b) {
                               return b.value ? std::string("true") : std::string("false");
                             },
                             [](const BigIntValue& bi) { return bi.value.toString(); },
                             [this](const ListValue& l) {
                               std::string result = "[";
                               for (size_t i = 0; i < l.elements.size(); ++i) {
                                 if (i > 0)
                                   result += ", ";
                                 result += valueToString(l.elements[i]);
                               }
                               result += "]";
                               return result;
                             },
                             [](const FunctionValue&) { return std::string("<function>"); },
                             [this](const RecordValue& r) {
                               std::string result = "{ ";
                               bool first = true;
                               for (const auto& [k, v] : r.fields) {
                                 if (!first)
                                   result += ", ";
                                 result += k + ": " + valueToString(v);
                                 first = false;
                               }
                               result += " }";
                               return result;
                             },
                             [this](const ThunkValue& thunk) {
                               return valueToString(thunk.force());
                             },
                             [this](const ConstructorValue& c) {
                               std::string result = c.name;
                               for (const auto& arg : c.args) {
                                 result += " " + valueToString(arg);
                               }
                               return result;
                             }},
                    forced->data);
}

std::vector<std::string> Interpreter::getBindingNames() const {
  std::vector<std::string> names;
  for (const auto& [name, _] : globalEnv_) {
    names.push_back(name);
  }
  return names;
}

bool Interpreter::hasBinding(const std::string& name) const {
  return globalEnv_.count(name) > 0;
}

ValuePtr Interpreter::getBinding(const std::string& name) const {
  auto it = globalEnv_.find(name);
  if (it != globalEnv_.end()) {
    return it->second;
  }
  return nullptr;
}

}  // namespace solis
