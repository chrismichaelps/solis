// Solis Programming Language - BigInt Runtime
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include <cstdint>
#include <gmp.h>
#include <string>

namespace solis {

/**
 * BigInt - Arbitrary precision integer using GMP backend
 *
 * Zero-overhead when not used. Only instantiated for literals with 'n' suffix
 * or explicit conversions from Int.
 */
class BigInt {
public:
  // Constructors
  BigInt();
  BigInt(int64_t value);
  BigInt(const std::string& str, int base = 10);
  BigInt(const BigInt& other);
  BigInt(BigInt&& other) noexcept;
  ~BigInt();

  // Assignment
  BigInt& operator=(const BigInt& other);
  BigInt& operator=(BigInt&& other) noexcept;

  // Arithmetic operations
  BigInt operator+(const BigInt& other) const;
  BigInt operator-(const BigInt& other) const;
  BigInt operator*(const BigInt& other) const;
  BigInt operator/(const BigInt& other) const;
  BigInt operator%(const BigInt& other) const;
  BigInt pow(uint64_t exp) const;  // For ^ operator
  BigInt operator-() const;        // Unary negation

  // Comparison operators
  bool operator==(const BigInt& other) const;
  bool operator!=(const BigInt& other) const;
  bool operator<(const BigInt& other) const;
  bool operator<=(const BigInt& other) const;
  bool operator>(const BigInt& other) const;
  bool operator>=(const BigInt& other) const;

  // Conversion
  std::string toString() const;
  int64_t toInt64() const;  // May throw if out of range
  bool fitsInInt64() const;

  // Direct access for interpreter (internal use)
  const mpz_t& value() const { return value_; }
  mpz_t& value() { return value_; }

private:
  mpz_t value_;
};

}  // namespace solis
