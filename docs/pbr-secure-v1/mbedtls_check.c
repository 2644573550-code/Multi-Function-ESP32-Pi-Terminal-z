/*
 * mbedtls conformance check for the pbr-secure-v1 vectors.
 *
 * Proves the Python-generated vectors reproduce with mbedtls alone, so ESP and
 * Pi implementations have a target that is not "whatever our code emits".
 *
 * Only the API surface shared by mbedtls 2.28 and 3.6 is used. In particular
 * this file never touches mbedtls_ecdh_context internals: 3.x dropped the
 * .d/.grp fields unless MBEDTLS_ECDH_LEGACY_CONTEXT is defined, and IDF does
 * not define it. mbedtls_ecp_mul() does the same computation and is public in
 * both versions.
 *
 * Build:
 *   cc -I<prefix>/include mbedtls_check.c -L<prefix>/lib -lmbedcrypto -o check
 *   ./check vectors.json
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/chachapoly.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

static int failures;

static void fail(const char *what) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
static void pass(const char *what) { printf("PASS: %s\n", what); }

/* mbedtls_mpi_set_bit returns int; chaining with || would short-circuit the
 * remaining steps and leave the scalar malformed. */
static int clamp_scalar(mbedtls_mpi *d) {
    int ret = mbedtls_mpi_set_bit(d, 0, 0);
    if (ret) return ret;
    if ((ret = mbedtls_mpi_set_bit(d, 1, 0))) return ret;
    if ((ret = mbedtls_mpi_set_bit(d, 2, 0))) return ret;
    if ((ret = mbedtls_mpi_set_bit(d, 255, 0))) return ret;
    return mbedtls_mpi_set_bit(d, 254, 1);
}

/* mbedtls 3.x rejects mbedtls_ecp_mul/mbedtls_ecdh_compute_shared with a NULL
 * f_rng (2.28 tolerated it). The RNG only feeds the blinding counter, so a
 * fixed stream keeps this conformance check reproducible. Real implementations
 * MUST pass the platform CSPRNG. */
static int det_rng(void *ctx, unsigned char *out, size_t len) {
    unsigned *state = ctx;
    for (size_t i = 0; i < len; ++i) {
        *state = *state * 1103515245u + 12345u;
        out[i] = (unsigned char)(*state >> 16);
    }
    return 0;
}

static size_t unhex(const char *hex, unsigned char *out, size_t cap) {
    const size_t n = strlen(hex) / 2;
    if (n > cap) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (unsigned char)v;
    }
    return n;
}

/* The file is generated one key per line, so a needle scan suffices and keeps
 * a JSON parser out of a conformance check. Handles \" so the plaintext field
 * can be compared verbatim. */
static int field(const char *buf, const char *key, char *out, size_t cap) {
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(buf, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '"') ++p;
    size_t i = 0;
    while (*p && i + 1 < cap) {
        if (*p == '\\' && p[1] == '"') { out[i++] = '"'; p += 2; continue; }
        if (*p == '"' || *p == ',' || *p == '\n' || *p == '}') break;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

/* n-th occurrence: several blocks reuse key names ("nonce", "aad",
 * "ciphertext", "tag", "signature_raw", "signed"). */
static int field_n(const char *buf, const char *key, int n, char *out, size_t cap) {
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = buf;
    for (int i = 0; i <= n; ++i) {
        p = strstr(p, needle);
        if (!p) return 0;
        if (i < n) p += strlen(needle);
    }
    return field(p, key, out, cap);
}

static int load(const char *buf, const char *key, int n, unsigned char *dst, size_t cap,
                size_t *len) {
    char hex[8192];
    if (!field_n(buf, key, n, hex, sizeof(hex))) {
        fprintf(stderr, "FAIL: missing %s[%d]\n", key, n); ++failures; return 0;
    }
    const size_t got = unhex(hex, dst, cap);
    if (!got) { fprintf(stderr, "FAIL: bad hex %s[%d]\n", key, n); ++failures; return 0; }
    if (len) *len = got;
    return 1;
}

/* X25519 shared secret: the same computation mbedtls_ecdh_compute_shared()
 * performs, which is mbedtls_ecp_mul() followed by taking the X coordinate. */
static int x25519(const unsigned char priv[32], const unsigned char peer[32],
                  unsigned char out[32]) {
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q, r;
    mbedtls_mpi d, z;
    unsigned rng_state = 1;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_ecp_point_init(&r);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);

    int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
    if (!ret) ret = mbedtls_mpi_read_binary_le(&d, priv, 32);
    /* RFC 7748 §5. mbedtls does not clamp a caller-supplied scalar; an
     * unclamped one is rejected as ECP_INVALID_KEY. */
    if (!ret) ret = clamp_scalar(&d);
    if (!ret) ret = mbedtls_ecp_point_read_binary(&grp, &q, peer, 32);
    if (!ret) ret = mbedtls_ecp_mul(&grp, &r, &d, &q, det_rng, &rng_state);
    if (!ret) {
        /* Point export is the portable route: touching r.X needs
         * MBEDTLS_PRIVATE() in 3.x and a bare .X in 2.28. For a Montgomery
         * point this writes exactly the 32-byte u-coordinate. */
        size_t olen = 0;
        ret = mbedtls_ecp_point_write_binary(&grp, &r, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                             &olen, out, 32);
        if (!ret && olen != 32) ret = -1;
    }

    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&r);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return ret;
}

static int aead_open(const unsigned char key[32], const unsigned char nonce[12],
                     const unsigned char *aad, size_t aad_len, const unsigned char tag[16],
                     const unsigned char *ct, size_t ct_len, unsigned char *out) {
    mbedtls_chachapoly_context cp;
    mbedtls_chachapoly_init(&cp);
    int ret = mbedtls_chachapoly_setkey(&cp, key);
    if (!ret) ret = mbedtls_chachapoly_auth_decrypt(&cp, ct_len, nonce, aad, aad_len, tag, ct, out);
    mbedtls_chachapoly_free(&cp);
    return ret;
}

static int ecdsa_verify_raw(const unsigned char pub[65], const unsigned char *msg, size_t msg_len,
                            const unsigned char sig[64]) {
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi r, s;
    unsigned char dig[32];
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    /* mbedtls_ecdsa_verify takes the digest, not the message: hashing is the
     * caller's job, unlike the Python side which hashes internally. */
    int ret = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), msg, msg_len, dig);
    if (!ret) ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    if (!ret) ret = mbedtls_ecp_point_read_binary(&grp, &q, pub, 65);
    if (!ret) ret = mbedtls_mpi_read_binary(&r, sig, 32);
    if (!ret) ret = mbedtls_mpi_read_binary(&s, sig + 32, 32);
    if (!ret) ret = mbedtls_ecdsa_verify(&grp, dig, sizeof(dig), &q, &r, &s);

    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return ret;
}

/* vector occurrence order in vectors.json:
 *   nonce/aad/ciphertext/tag   0 = hello_ok, 1 = business frame
 *   signature_raw              0 = pi (over T3), 1 = bridge (over T2)
 *   must_verify_with           0 = pi pub, 1 = bridge pub
 *   signed                     0 = T3, 1 = T2
 */
static const char *kHelloOk =
    "{\"schema\":\"pbr-handshake-v1\",\"msg\":\"hello_ok\",\"uptime_ms\":12345}";

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s vectors.json\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static char buf[1 << 16];
    const size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = '\0';

    static unsigned char pi_priv[32], br_eph_priv[32], br_eph_pub[32], shared_ref[32];
    static unsigned char want_hash[32], want_k[96], prefix[4];
    static unsigned char ok_nonce[12], ok_aad[32], ok_ct[512], ok_tag[16];
    static unsigned char nonce[12], aad[32], ct[512], tag[16];
    static unsigned char pi_sig[64], br_sig[64], pi_pub[65], br_pub[65];
    static unsigned char t3[1024], t2[1024], plain[512];
    size_t ok_ct_len = 0, ct_len = 0, t3_len = 0, t2_len = 0;
    size_t ok_aad_len = 0, aad_len = 0;

    if (!load(buf, "pi_eph_priv", 0, pi_priv, sizeof(pi_priv), NULL)) return 1;
    if (!load(buf, "bridge_eph_priv", 0, br_eph_priv, sizeof(br_eph_priv), NULL)) return 1;
    if (!load(buf, "bridge_eph_pub", 0, br_eph_pub, sizeof(br_eph_pub), NULL)) return 1;
    if (!load(buf, "x25519_shared", 0, shared_ref, sizeof(shared_ref), NULL)) return 1;
    if (!load(buf, "transcript_hash", 0, want_hash, sizeof(want_hash), NULL)) return 1;
    if (!load(buf, "hkdf_96", 0, want_k, sizeof(want_k), NULL)) return 1;
    if (!load(buf, "nonce_prefix", 0, prefix, sizeof(prefix), NULL)) return 1;
    if (!load(buf, "nonce", 0, ok_nonce, sizeof(ok_nonce), NULL)) return 1;
    if (!load(buf, "aad", 0, ok_aad, sizeof(ok_aad), &ok_aad_len)) return 1;
    if (!load(buf, "ciphertext", 0, ok_ct, sizeof(ok_ct), &ok_ct_len)) return 1;
    if (!load(buf, "tag", 0, ok_tag, sizeof(ok_tag), NULL)) return 1;
    if (!load(buf, "nonce", 1, nonce, sizeof(nonce), NULL)) return 1;
    if (!load(buf, "aad", 1, aad, sizeof(aad), &aad_len)) return 1;
    if (!load(buf, "ciphertext", 1, ct, sizeof(ct), &ct_len)) return 1;
    if (!load(buf, "tag", 1, tag, sizeof(tag), NULL)) return 1;
    if (!load(buf, "signature_raw", 0, pi_sig, sizeof(pi_sig), NULL)) return 1;
    if (!load(buf, "signature_raw", 1, br_sig, sizeof(br_sig), NULL)) return 1;
    if (!load(buf, "must_verify_with", 0, pi_pub, sizeof(pi_pub), NULL)) return 1;
    if (!load(buf, "must_verify_with", 1, br_pub, sizeof(br_pub), NULL)) return 1;
    if (!load(buf, "signed", 0, t3, sizeof(t3), &t3_len)) return 1;
    if (!load(buf, "signed", 1, t2, sizeof(t2), &t2_len)) return 1;

    /* ---- 1. X25519 ------------------------------------------------------ */
    /* The scalar is little-endian. Reading it big-endian yields an
     * out-of-range scalar and mbedtls reports ECP_INVALID_KEY, so this is the
     * first thing an implementer gets wrong. */
    unsigned char shared[32] = {0};
    if (x25519(pi_priv, br_eph_pub, shared) != 0) fail("x25519 compute");
    else if (memcmp(shared, shared_ref, sizeof(shared_ref)) != 0) fail("x25519 shared secret differs");
    else pass("x25519 matches x25519_shared");

    /* the derived nonce prefix must be the documented slice of the hash */
    if (memcmp(prefix, want_hash + 16, sizeof(prefix)) != 0)
        fail("nonce_prefix is not transcript_hash[16..20)");
    else pass("nonce_prefix matches transcript_hash[16..20)");

    /* ---- 2. HKDF-SHA256 with the exact info string ---------------------- */
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    static const unsigned char info[] = "pbr-secure-v1";
    unsigned char got_k[96];
    if (mbedtls_hkdf(sha256, want_hash, 32, shared, 32,
                     info, sizeof(info) - 1, got_k, sizeof(got_k)) != 0)
        fail("hkdf");
    else if (memcmp(got_k, want_k, sizeof(want_k)) != 0)
        fail("hkdf output differs (info string or salt mismatch)");
    else
        pass("hkdf-sha256 matches hkdf_96");

    /* ---- 3. hello_ok, sealed with k_handshake --------------------------- */
    const size_t hello_len = strlen(kHelloOk);
    if (aead_open(want_k, ok_nonce, ok_aad, ok_aad_len, ok_tag, ok_ct, hello_len, plain) != 0)
        fail("hello_ok tag rejected (nonce/aad/key order differs)");
    else if (memcmp(plain, kHelloOk, hello_len) != 0)
        fail("hello_ok plaintext differs");
    else
        pass("hello_ok decrypts with k_handshake");

    /* ---- 4. business frame, sealed with k_bridge_to_pi ------------------ */
    if (aead_open(want_k + 64, nonce, aad, aad_len, tag, ct, ct_len, plain) != 0)
        fail("chachapoly tag rejected (nonce/aad/key order differs)");
    else if (memcmp(plain, "{\"event\":\"attach\",\"module_id\":80,"
                           "\"module_instance\":\"planter-a\"}", ct_len) != 0)
        fail("chachapoly plaintext differs");
    else
        pass("chachapoly decrypts to the expected plaintext with k_bridge_to_pi");

    /* ---- 5. both identity signatures, raw r||s ------------------------- */
    if (ecdsa_verify_raw(pi_pub, t3, t3_len, pi_sig) != 0)
        fail("pi ecdsa verify (raw r||s mapping wrong)");
    else
        pass("pi ecdsa verifies raw r||s against T3");

    if (ecdsa_verify_raw(br_pub, t2, t2_len, br_sig) != 0)
        fail("bridge ecdsa verify (mutual auth would silently degrade)");
    else
        pass("bridge ecdsa verifies raw r||s against T2");

    printf(failures ? "\n%d check(s) failed\n" : "\nall mbedtls checks passed\n", failures);
    return failures ? 1 : 0;
}
