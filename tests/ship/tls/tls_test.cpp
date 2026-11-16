// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tls_test.cpp
 * @brief Unit tests for SHIP TLS Subject Key Identifier validation.
 */

#include <gtest/gtest.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "src/ship/tls/ship_tls.h"
}

namespace {

EVP_PKEY* generate_ec_key() {
  EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
  if (!context) return nullptr;

  EVP_PKEY* key = nullptr;
  if (EVP_PKEY_keygen_init(context) != 1 ||
      EVP_PKEY_CTX_set_ec_paramgen_curve_nid(
          context, NID_X9_62_prime256v1) != 1 ||
      EVP_PKEY_keygen(context, &key) != 1) {
    EVP_PKEY_free(key);
    key = nullptr;
  }
  EVP_PKEY_CTX_free(context);
  return key;
}

X509* make_certificate(EVP_PKEY* key, const char* ski_value) {
  if (!key) return nullptr;

  X509* certificate = X509_new();
  if (!certificate || X509_set_version(certificate, 2) != 1 ||
      ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1) != 1 ||
      !X509_gmtime_adj(X509_get_notBefore(certificate), 0) ||
      !X509_gmtime_adj(X509_get_notAfter(certificate), 3600) ||
      X509_set_pubkey(certificate, key) != 1) {
    X509_free(certificate);
    return nullptr;
  }

  X509_NAME* name = X509_get_subject_name(certificate);
  if (!name || X509_NAME_add_entry_by_txt(
                   name, "CN", MBSTRING_ASC,
                   reinterpret_cast<const unsigned char*>("TlsTest"),
                   -1, -1, 0) != 1 ||
      X509_set_issuer_name(certificate, name) != 1) {
    X509_free(certificate);
    return nullptr;
  }

  X509V3_CTX extension_context;
  X509V3_set_ctx_nodb(&extension_context);
  X509V3_set_ctx(&extension_context, certificate, certificate,
                 nullptr, nullptr, 0);

  if (ski_value) {
    X509_EXTENSION* extension = X509V3_EXT_conf_nid(
        nullptr, &extension_context, NID_subject_key_identifier, ski_value);
    if (!extension || X509_add_ext(certificate, extension, -1) != 1) {
      X509_EXTENSION_free(extension);
      X509_free(certificate);
      return nullptr;
    }
    X509_EXTENSION_free(extension);
  }

  if (X509_sign(certificate, key, EVP_sha256()) <= 0) {
    X509_free(certificate);
    return nullptr;
  }
  return certificate;
}

std::string certificate_to_pem(X509* certificate) {
  if (!certificate) return {};

  BIO* bio = BIO_new(BIO_s_mem());
  if (!bio || PEM_write_bio_X509(bio, certificate) != 1) {
    BIO_free(bio);
    return {};
  }

  char* data = nullptr;
  const long length = BIO_get_mem_data(bio, &data);
  std::string result(data, length > 0 ? static_cast<size_t>(length) : 0U);
  BIO_free(bio);
  return result;
}

std::vector<uint8_t> certificate_to_der(X509* certificate) {
  if (!certificate) return {};

  unsigned char* der = nullptr;
  const int length = i2d_X509(certificate, &der);
  if (length <= 0 || !der) return {};

  std::vector<uint8_t> result(der, der + length);
  OPENSSL_free(der);
  return result;
}

}  // namespace

TEST(TlsSkiTest, ValidatesSubjectKeyIdentifierAgainstPublicKey) {
  EVP_PKEY* key = generate_ec_key();
  ASSERT_NE(nullptr, key);
  X509* certificate = make_certificate(key, "hash");
  ASSERT_NE(nullptr, certificate);

  const std::string pem = certificate_to_pem(certificate);
  const std::vector<uint8_t> der = certificate_to_der(certificate);
  ASSERT_FALSE(pem.empty());
  ASSERT_FALSE(der.empty());

  char ski_from_pem[SHIP_SKI_STR_LEN] = {0};
  char ski_from_der[SHIP_SKI_STR_LEN] = {0};
  EXPECT_EQ(EEBUS_OK, ship_tls_compute_ski(pem.c_str(), ski_from_pem));
  EXPECT_EQ(EEBUS_OK, ship_tls_compute_ski_from_der(
                           der.data(), der.size(), ski_from_der));
  EXPECT_STREQ(ski_from_pem, ski_from_der);

  X509_free(certificate);
  EVP_PKEY_free(key);
}

TEST(TlsSkiTest, RejectsMissingSubjectKeyIdentifier) {
  EVP_PKEY* key = generate_ec_key();
  ASSERT_NE(nullptr, key);
  X509* certificate = make_certificate(key, nullptr);
  ASSERT_NE(nullptr, certificate);

  const std::string pem = certificate_to_pem(certificate);
  const std::vector<uint8_t> der = certificate_to_der(certificate);
  char ski[SHIP_SKI_STR_LEN] = {0};
  EXPECT_EQ(EEBUS_ERR_CERT, ship_tls_compute_ski(pem.c_str(), ski));
  EXPECT_EQ(EEBUS_ERR_CERT, ship_tls_compute_ski_from_der(
                                 der.data(), der.size(), ski));

  X509_free(certificate);
  EVP_PKEY_free(key);
}

TEST(TlsSkiTest, RejectsMismatchedSubjectKeyIdentifier) {
  EVP_PKEY* key = generate_ec_key();
  ASSERT_NE(nullptr, key);
  X509* certificate = make_certificate(
      key, "00112233445566778899aabbccddeeff00112233");
  ASSERT_NE(nullptr, certificate);

  const std::string pem = certificate_to_pem(certificate);
  const std::vector<uint8_t> der = certificate_to_der(certificate);
  char ski[SHIP_SKI_STR_LEN] = {0};
  EXPECT_EQ(EEBUS_ERR_CERT, ship_tls_compute_ski(pem.c_str(), ski));
  EXPECT_EQ(EEBUS_ERR_CERT, ship_tls_compute_ski_from_der(
                                 der.data(), der.size(), ski));

  X509_free(certificate);
  EVP_PKEY_free(key);
}

TEST(TlsSkiTest, RejectsTrailingDerData) {
  EVP_PKEY* key = generate_ec_key();
  ASSERT_NE(nullptr, key);
  X509* certificate = make_certificate(key, "hash");
  ASSERT_NE(nullptr, certificate);

  std::vector<uint8_t> der = certificate_to_der(certificate);
  ASSERT_FALSE(der.empty());
  der.push_back(0);

  char ski[SHIP_SKI_STR_LEN] = {0};
  EXPECT_EQ(EEBUS_ERR_CERT, ship_tls_compute_ski_from_der(
                                 der.data(), der.size(), ski));

  X509_free(certificate);
  EVP_PKEY_free(key);
}
