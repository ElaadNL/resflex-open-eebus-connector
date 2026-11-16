// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_tls_openssl.c
 * @brief OpenSSL backend for SHIP TLS certificate management.
 *
 * SHIP §891 → RFC 3280 §4.2.1.2 method (1): SKI = SHA-1 of the BIT STRING
 * subjectPublicKey value (i.e. the raw EC point 04||X||Y, NOT the full SPKI).
 * The certificate SKI extension is validated against the value recomputed from
 * the certificate's public key before it is accepted as a SHIP identity.
 *
 * Build guard: compiled only when EEBUS_TLS_BACKEND_OPENSSL is defined.
 */

#ifdef EEBUS_TLS_BACKEND_OPENSSL

#include "ship_tls.h"
#include "../../common/eebus_memory.h"

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Certificate validity: 10 years in seconds */
#define CERT_VALIDITY_SECONDS (10L * 365 * 24 * 60 * 60)

/* ── Internal helpers ────────────────────────────────────────────────────── */

/**
 * @brief Convert a BIO containing PEM data to a heap-allocated C string.
 *
 * @param bio  BIO to read from.
 * @return     Heap-allocated NUL-terminated string, or NULL on error.
 */
static char* bio_to_string(BIO* bio) {
  size_t  len  = (size_t)BIO_pending(bio);
  char*   buf  = EEBUS_MALLOC(len + 1);
  if (!buf) return NULL;
  BIO_read(bio, buf, (int)len);
  buf[len] = '\0';
  return buf;
}

/**
 * @brief Derive the RFC 3280 method (1) SKI from an X.509 public key.
 *
 * SKI is SHA-1(value of the subjectPublicKey BIT STRING). The ASN.1 tag,
 * length, unused-bits count, and SubjectPublicKeyInfo algorithm identifier
 * are not part of the hash input.
 */
static eebus_err_t compute_public_key_ski_from_x509(
    const X509* cert, unsigned char out[SHA_DIGEST_LENGTH]) {
  if (!cert || !out) return EEBUS_ERR_INVALID;

  const ASN1_BIT_STRING* public_key = X509_get0_pubkey_bitstr(cert);
  const int length = public_key ? ASN1_STRING_length(public_key) : 0;
  const unsigned char* data =
      public_key ? ASN1_STRING_get0_data(public_key) : NULL;

  if (!data || length <= 0 || !SHA1(data, (size_t)length, out)) {
    return EEBUS_ERR_CERT;
  }

  return EEBUS_OK;
}

/**
 * @brief Extract and verify the SKI extension of an X.509 certificate.
 *
 * A valid SHIP certificate must contain a 20-byte SubjectKeyIdentifier
 * extension equal to SHA-1(subjectPublicKey BIT STRING).
 */
static eebus_err_t compute_ski_from_x509(
    X509* cert, char ski_out[SHIP_SKI_STR_LEN]) {
  if (!cert || !ski_out) return EEBUS_ERR_INVALID;

  const ASN1_OCTET_STRING* ski = X509_get0_subject_key_id(cert);
  if (!ski || ASN1_STRING_length(ski) != SHA_DIGEST_LENGTH) {
    return EEBUS_ERR_CERT;
  }

  unsigned char derived_ski[SHA_DIGEST_LENGTH];
  eebus_err_t result =
      compute_public_key_ski_from_x509(cert, derived_ski);
  if (result != EEBUS_OK) {
    OPENSSL_cleanse(derived_ski, sizeof(derived_ski));
    return result;
  }

  const unsigned char* extension_ski = ASN1_STRING_get0_data(ski);
  if (!extension_ski ||
      CRYPTO_memcmp(extension_ski, derived_ski, SHA_DIGEST_LENGTH) != 0) {
    OPENSSL_cleanse(derived_ski, sizeof(derived_ski));
    return EEBUS_ERR_CERT;
  }
  OPENSSL_cleanse(derived_ski, sizeof(derived_ski));

  for (size_t index = 0U; index < SHA_DIGEST_LENGTH; ++index) {
    snprintf(ski_out + index * 2U, 3U, "%02x", extension_ski[index]);
  }
  ski_out[SHIP_SKI_STR_LEN - 1] = '\0';
  return EEBUS_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

eebus_err_t ship_tls_generate(ship_tls_cred_t* cred, const char* common_name) {
  if (!cred) return EEBUS_ERR_INVALID;
  memset(cred, 0, sizeof(*cred));

  if (!common_name || common_name[0] == '\0') {
    common_name = "EEBus Device";
  }

  eebus_err_t  result = EEBUS_ERR_TLS;
  EVP_PKEY*    pkey   = NULL;
  EVP_PKEY_CTX* kctx  = NULL;
  X509*        x509   = NULL;
  BIO*         bio    = NULL;

  /* ── Generate ECDSA P-256 key pair (OpenSSL 1.1 + 3.x compatible) ─────── */
  kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
  if (!kctx) goto cleanup;
  if (EVP_PKEY_keygen_init(kctx) <= 0) goto cleanup;
  if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, NID_X9_62_prime256v1) <= 0)
    goto cleanup;
  if (EVP_PKEY_keygen(kctx, &pkey) <= 0) goto cleanup;
  EVP_PKEY_CTX_free(kctx);
  kctx = NULL;

  /* ── Create and configure the certificate ─────────────────────────────── */
  x509 = X509_new();
  if (!x509) goto cleanup;

  /* MUST be v3 (value=2) – extensions are only legal in X.509 v3. */
  X509_set_version(x509, 2);

  /* Serial number: current time (simple but non-sequential) */
  ASN1_INTEGER_set(X509_get_serialNumber(x509), (long)time(NULL));

  /* Validity window: now → now + 10 years */
  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  X509_gmtime_adj(X509_get_notAfter(x509), CERT_VALIDITY_SECONDS);

  /* Public key */
  X509_set_pubkey(x509, pkey);

  /* Subject / Issuer (self-signed → same) */
  X509_NAME* name = X509_get_subject_name(x509);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                              (const unsigned char*)common_name, -1, -1, 0);
  X509_set_issuer_name(x509, name);

  /* ── X.509v3 extensions ──────────────────────────────────────────────────
   *
   * Modern TLS implementations (including the official EEBus test tool)
   * enforce RFC 6125 / RFC 5280 requirements that were historically optional:
   *
   *  1. SubjectAltName (SAN)  – RFC 6125 §6.4: clients MUST use SAN for
   *     identity matching and SHOULD ignore CN when SAN is present.
   *     Without a SAN, Java, Go, and other strict TLS stacks send a
   *     certificate_unknown alert and abort the handshake immediately.
   *
   *  2. ExtendedKeyUsage (EKU) – RFC 5280 §4.2.1.12: a certificate used for
   *     TLS authentication should carry id-kp-serverAuth and id-kp-clientAuth.
   *     SHIP nodes act as both server and client depending on the connection
   *     direction, so both OIDs are required.
   *
   *  3. BasicConstraints – RFC 5280 §4.2.1.9: CA:FALSE marks this as an end-
   *     entity certificate, which is mandatory for leaf certificates.
   *
   *  4. SubjectKeyIdentifier – RFC 3280 §4.2.1.2 method (1):
   *     SHA-1(BIT STRING subjectPublicKey value) = SHA-1(04||X||Y).
   *     OpenSSL's "hash" directive implements this correctly.  The resulting
   *     extension value is later validated and read back by
   *     compute_ski_from_x509() so that the SKI we advertise in SHIP Hello is
   *     identical to what the peer sees.
   */
  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, x509, x509, NULL, NULL, 0);

  /* 1. SubjectKeyIdentifier */
  X509_EXTENSION* ski_ext =
      X509V3_EXT_conf_nid(NULL, &ctx, NID_subject_key_identifier, "hash");
  if (ski_ext) {
    X509_add_ext(x509, ski_ext, -1);
    X509_EXTENSION_free(ski_ext);
  }

  /* 2. SubjectAltName: DNS:<common_name>
   *
   * RFC 6125 requires at least one dNSName SAN.  We use the common_name
   * string supplied by the caller (e.g. "EEBus Device").  For IP-only
   * networks an IP SAN would be more appropriate, but mDNS-based SHIP
   * discovery always uses hostnames, so DNS is correct here.            */
  {
    /* Build the SAN value string: "DNS:<common_name>" */
    char san_val[512];
    snprintf(san_val, sizeof(san_val), "DNS:%s", common_name);
    X509_EXTENSION* san_ext =
        X509V3_EXT_conf_nid(NULL, &ctx, NID_subject_alt_name, san_val);
    if (san_ext) {
      X509_add_ext(x509, san_ext, -1);
      X509_EXTENSION_free(san_ext);
    }
  }

  /* 3. ExtendedKeyUsage: serverAuth + clientAuth
   *
   * SHIP nodes originate both inbound (server) and outbound (client)
   * connections depending on which side discovered the other via mDNS.
   * Both OIDs must be present so the TLS stack accepts the certificate
   * regardless of the connection direction.                              */
  {
    X509_EXTENSION* eku_ext = X509V3_EXT_conf_nid(
        NULL, &ctx, NID_ext_key_usage,
        "serverAuth,clientAuth");
    if (eku_ext) {
      X509_add_ext(x509, eku_ext, -1);
      X509_EXTENSION_free(eku_ext);
    }
  }

  /* 4. BasicConstraints: CA:FALSE (RFC 5280 §4.2.1.9)
   *
   * SHIP §12.1 requires all nodes to accept self-signed certificates, but
   * the trust decision is made at the SHIP Hello layer via SKI comparison,
   * not at the PKI layer. The TLS verification callback ignores the PKI
   * chain verdict but rejects certificates with an invalid SKI binding.
   *
   * CA:FALSE is correct for an end-entity certificate and prevents the cert
   * from being usable as a signing certificate for subordinate CAs even if
   * it ends up in someone's trust store.
   *
   * Previous versions used CA:TRUE, pathlen:0 to work around an OpenSSL
   * behaviour where a non-CA self-signed cert in the server's trust store
   * did not trigger CertificateRequest.  The current implementation relies
   * on SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT (set in the LWS
   * callback) to mandate client cert transmission; that flag is independent
   * of the CA bit and works correctly with CA:FALSE.                        */
  {
    X509_EXTENSION* bc_ext = X509V3_EXT_conf_nid(
        NULL, &ctx, NID_basic_constraints, "critical,CA:FALSE");
    if (bc_ext) {
      X509_add_ext(x509, bc_ext, -1);
      X509_EXTENSION_free(bc_ext);
    }
  }

  /* Sign with our private key */
  if (!X509_sign(x509, pkey, EVP_sha256())) goto cleanup;

  /* ── Export certificate to PEM ────────────────────────────────────────── */
  bio = BIO_new(BIO_s_mem());
  if (!bio) goto cleanup;
  PEM_write_bio_X509(bio, x509);
  cred->cert_pem = bio_to_string(bio);
  BIO_free(bio);
  bio = NULL;
  if (!cred->cert_pem) goto cleanup;

  /* ── Export private key to PEM ────────────────────────────────────────── */
  bio = BIO_new(BIO_s_mem());
  if (!bio) goto cleanup;
  PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL);
  cred->key_pem = bio_to_string(bio);
  BIO_free(bio);
  bio = NULL;
  if (!cred->key_pem) goto cleanup;

  /* ── Compute SKI ──────────────────────────────────────────────────────── */
  if (compute_ski_from_x509(x509, cred->ski) != EEBUS_OK) goto cleanup;

  result = EEBUS_OK;

cleanup:
  EVP_PKEY_CTX_free(kctx);
  EVP_PKEY_free(pkey);
  X509_free(x509);
  BIO_free(bio);

  if (result != EEBUS_OK) {
    ship_tls_cred_free(cred);
  }
  return result;
}

eebus_err_t ship_tls_load(ship_tls_cred_t* cred, const char* cert_path, const char* key_path) {
  if (!cred || !cert_path || !key_path) return EEBUS_ERR_INVALID;
  memset(cred, 0, sizeof(*cred));

  eebus_err_t result = EEBUS_ERR_IO;

  /* ── Read certificate file ────────────────────────────────────────────── */
  FILE* f = fopen(cert_path, "rb");
  if (!f) return EEBUS_ERR_IO;

  fseek(f, 0, SEEK_END);
  long cert_sz = ftell(f);
  rewind(f);

  cred->cert_pem = EEBUS_MALLOC((size_t)cert_sz + 1);
  if (!cred->cert_pem) { fclose(f); return EEBUS_ERR_NO_MEMORY; }

  if (fread(cred->cert_pem, 1, (size_t)cert_sz, f) != (size_t)cert_sz) {
    fclose(f);
    goto cleanup;
  }
  cred->cert_pem[cert_sz] = '\0';
  fclose(f);

  /* ── Read key file ────────────────────────────────────────────────────── */
  f = fopen(key_path, "rb");
  if (!f) goto cleanup;

  fseek(f, 0, SEEK_END);
  long key_sz = ftell(f);
  rewind(f);

  cred->key_pem = EEBUS_MALLOC((size_t)key_sz + 1);
  if (!cred->key_pem) { fclose(f); goto cleanup; }

  if (fread(cred->key_pem, 1, (size_t)key_sz, f) != (size_t)key_sz) {
    fclose(f);
    goto cleanup;
  }
  cred->key_pem[key_sz] = '\0';
  fclose(f);

  /* ── Compute SKI from loaded certificate ──────────────────────────────── */
  result = ship_tls_compute_ski(cred->cert_pem, cred->ski);
  if (result != EEBUS_OK) goto cleanup;

  /* ── Store filesystem paths for the libwebsockets file-path TLS API ───── */
  snprintf(cred->cert_path, SHIP_TLS_PATH_MAX, "%s", cert_path);
  snprintf(cred->key_path,  SHIP_TLS_PATH_MAX, "%s", key_path);

  return EEBUS_OK;

cleanup:
  ship_tls_cred_free(cred);
  return result;
}

eebus_err_t ship_tls_save(ship_tls_cred_t* cred, const char* cert_path, const char* key_path) {
  if (!cred || !cert_path || !key_path) return EEBUS_ERR_INVALID;
  if (!cred->cert_pem || !cred->key_pem)  return EEBUS_ERR_INVALID;

  FILE* f = fopen(cert_path, "wb");
  if (!f) return EEBUS_ERR_IO;
  fputs(cred->cert_pem, f);
  fclose(f);

  f = fopen(key_path, "wb");
  if (!f) return EEBUS_ERR_IO;
  fputs(cred->key_pem, f);
  fclose(f);

  /* Store paths so the credential can be used with the file-path TLS API */
  snprintf(cred->cert_path, SHIP_TLS_PATH_MAX, "%s", cert_path);
  snprintf(cred->key_path,  SHIP_TLS_PATH_MAX, "%s", key_path);

  return EEBUS_OK;
}

eebus_err_t ship_tls_compute_ski(const char* cert_pem, char ski_out[SHIP_SKI_STR_LEN]) {
  if (!cert_pem || !ski_out) return EEBUS_ERR_INVALID;

  BIO* bio = BIO_new_mem_buf(cert_pem, -1);
  if (!bio) return EEBUS_ERR_TLS;

  X509* cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
  BIO_free(bio);
  if (!cert) return EEBUS_ERR_CERT;

  eebus_err_t rc = compute_ski_from_x509(cert, ski_out);
  X509_free(cert);
  return rc;
}

eebus_err_t ship_tls_compute_ski_from_der(const uint8_t* der, size_t der_len,
                                          char ski_out[SHIP_SKI_STR_LEN]) {
  if (!der || der_len == 0 || !ski_out) return EEBUS_ERR_INVALID;

  /* d2i_X509 advances the pointer; reject trailing DER data. */
  const unsigned char* p = (const unsigned char*)der;
  X509* cert = d2i_X509(NULL, &p, (long)der_len);
  if (!cert || (size_t)(p - der) != der_len) {
    X509_free(cert);
    return EEBUS_ERR_CERT;
  }

  eebus_err_t rc = compute_ski_from_x509(cert, ski_out);
  X509_free(cert);
  return rc;
}

eebus_err_t ship_tls_verify_ski(const char* cert_pem, const char* expected_ski) {
  if (!cert_pem || !expected_ski) return EEBUS_ERR_INVALID;

  char actual_ski[SHIP_SKI_STR_LEN];
  eebus_err_t rc = ship_tls_compute_ski(cert_pem, actual_ski);
  if (rc != EEBUS_OK) return rc;

  return (strncmp(actual_ski, expected_ski, SHIP_SKI_STR_LEN) == 0)
             ? EEBUS_OK
             : EEBUS_ERR_CERT;
}

void ship_tls_cred_free(ship_tls_cred_t* cred) {
  if (!cred) return;
  EEBUS_FREE(cred->cert_pem);
  EEBUS_FREE(cred->key_pem);
  memset(cred, 0, sizeof(*cred));
}

#endif /* EEBUS_TLS_BACKEND_OPENSSL */

/* Suppress ISO C "empty translation unit" warning when the build guard above
 * is not defined (e.g. in unit-test / stub builds). */
typedef int ship_tls_openssl_dummy_t;
