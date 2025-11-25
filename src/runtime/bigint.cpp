// Solis Programming Language - BigInt Runtime Implementation
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// Arbitrary-precision integer arithmetic using GMP library
// Supports all standard integer operations with unlimited precision

#include "runtime/bigint.hpp"

#include <cstring>
#include <stdexcept>

namespace solis {

// Constructors
BigInt::BigInt() {
  mpz_init(value_);
}

BigInt::BigInt(int64_t value) {
  mpz_init_set_si(value_, value);
}

BigInt::BigInt(const std::string& str, int base) {
  if (mpz_init_set_str(value_, str.c_str(), base) != 0) {
    mpz_clear(value_);
    throw std::invalid_argument("Invalid BigInt string: " + str);
  }
}

BigInt::BigInt(const BigInt& other) {
  mpz_init_set(value_, other.value_);
}

BigInt::BigInt(BigInt&& other) noexcept {
  mpz_init(value_);
  mpz_swap(value_, other.value_);
}

BigInt::~BigInt() {
  mpz_clear(value_);
}

// Assignment
BigInt& BigInt::operator=(const BigInt& other) {
  if (this != &other) {
    mpz_set(value_, other.value_);
  }
  return *this;
}

BigInt& BigInt::operator=(BigInt&& other) noexcept {
  if (this != &other) {
    mpz_swap(value_, other.value_);
  }
  return *this;
}

// Arithmetic
BigInt BigInt::operator+(const BigInt& other) const {
  BigInt result;
  mpz_add(result.value_, value_, other.value_);
  return result;
}

BigInt BigInt::operator-(const BigInt& other) const {
  BigInt result;
  mpz_sub(result.value_, value_, other.value_);
  return result;
}

BigInt BigInt::operator*(const BigInt& other) const {
  BigInt result;
  mpz_mul(result.value_, value_, other.value_);
  return result;
}

BigInt BigInt::operator/(const BigInt& other) const {
  BigInt result;
  mpz_fdiv_q(result.value_, value_, other.value_);  // Floor division
  return result;
}

BigInt BigInt::operator%(const BigInt& other) const {
  BigInt result;
  mpz_fdiv_r(result.value_, value_, other.value_);
  return result;
}

BigInt BigInt::pow(uint64_t exp) const {
  BigInt result;
  mpz_pow_ui(result.value_, value_, exp);
  return result;
}

BigInt BigInt::operator-() const {
  BigInt result;
  mpz_neg(result.value_, value_);
  return result;
}

// Comparison
bool BigInt::operator==(const BigInt& other) const {
  return mpz_cmp(value_, other.value_) == 0;
}

bool BigInt::operator!=(const BigInt& other) const {
  return mpz_cmp(value_, other.value_) != 0;
}

bool BigInt::operator<(const BigInt& other) const {
  return mpz_cmp(value_, other.value_) < 0;
}

bool BigInt::operator<=(const BigInt& other) const {
  return mpz_cmp(value_, other.value_) <= 0;
}

bool BigInt::operator>(const BigInt& other) const {
  return mpz_cmp(value_, other.value_) > 0;
}

bool BigInt::operator>=(const BigInt& other) const {
  return mpz_cmp(value_, other.value_) >= 0;
}

// Conversion
std::string BigInt::toString() const {
  char* str = mpz_get_str(nullptr, 10, value_);
  std::string result(str);
  free(str);  // GMP allocates with malloc
  return result;
}

int64_t BigInt::toInt64() const {
  if (!fitsInInt64()) {
    throw std::overflow_error("BigInt too large for int64_t: " + toString());
  }
  return mpz_get_si(value_);
}

bool BigInt::fitsInInt64() const {
  return mpz_fits_slong_p(value_);
}

}  // namespace solis
