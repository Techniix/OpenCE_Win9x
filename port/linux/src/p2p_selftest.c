/*
P2P_SELFTEST.C

Known-answer tests of what internet play seals its tunnel with (p2p_crypto.c):
SHA-256, HMAC-SHA-256, ChaCha20-Poly1305, X25519 and Ed25519, each against
the published test vectors (RFC 4231, RFC 8439, RFC 7748, RFC 8032), then two
machines' keys agreeing on a secret and a sealed message surviving the trip
and refusing a changed byte; and the machine's random numbers (posix.h). The
same code runs on the host as a test (port/linux/tests/p2p_selftest_test.py)
and on the target as halo.exe's self test (selftest.c): a failure there
where the host passes is the target's compiler, processor or system, not the
protocol.
*/

#include "posix.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <string.h>

/* ---------- private code */

static int hex_bytes(const char *text, unsigned char *bytes, int maximum)
{
	int count = 0;

	while (text[0] && text[1] && count < maximum)
	{
		unsigned int value = 0;
		int index;

		for (index = 0; index < 2; index++)
		{
			char c = text[index];

			value = value << 4 | (unsigned int)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
		}
		bytes[count++] = (unsigned char)value;
		text += 2;
	}
	return count;
}

static int same_hex(const unsigned char *bytes, int size, const char *hex)
{
	unsigned char expected[256];

	return hex_bytes(hex, expected, sizeof(expected)) == size && !memcmp(bytes, expected, (size_t)size);
}

static void check(p2p_selftest_report report, void *context, const char *name, int ok, int *failures)
{
	if (!ok)
		(*failures)++;
	report(name, ok, context);
}

/* ---------- public code */

int p2p_selftest(p2p_selftest_report report, void *context)
{
	int failures = 0;
	unsigned char key[32], nonce[12], buffer[256], out[256], digest[64], sealed[256];

	/* SHA-256 of "abc" */
	p2p_sha256("abc", 3, digest);
	check(report, context, "SHA-256 (RFC 6234)", same_hex(digest, 32,
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), &failures);

	/* HMAC-SHA-256, RFC 4231 test case 2 */
	p2p_hmac_sha256((const unsigned char *)"Jefe", 4, "what do ya want for nothing?", 28, digest);
	check(report, context, "HMAC-SHA-256 (RFC 4231)", same_hex(digest, 32,
		"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"), &failures);

	/* ChaCha20-Poly1305, RFC 8439 section 2.8.2 */
	{
		static const char plaintext[] = "Ladies and Gentlemen of the class of '99: If I could offer you "
			"only one tip for the future, sunscreen would be it.";
		unsigned char additional[12];
		int size = (int)sizeof(plaintext) - 1, sealed_size, index, opened;

		for (index = 0; index < 32; index++)
			key[index] = (unsigned char)(0x80 + index);
		hex_bytes("070000004041424344454647", nonce, 12);
		hex_bytes("50515253c0c1c2c3c4c5c6c7", additional, 12);
		sealed_size = p2p_aead_seal(key, nonce, additional, 12, plaintext, size, sealed);
		check(report, context, "ChaCha20-Poly1305 seal (RFC 8439)", sealed_size == size + 16 &&
			same_hex(sealed, 16, "d31a8d34648e60db7b86afbc53ef7ec2") &&
			same_hex(sealed + size, 16, "1ae10b594f09e26a7e902ecbd0600691"), &failures);
		opened = p2p_aead_open(key, nonce, additional, 12, sealed, sealed_size, out);
		check(report, context, "ChaCha20-Poly1305 open", opened == size && !memcmp(out, plaintext, (size_t)size),
			&failures);
		sealed[5] ^= 1;
		check(report, context, "ChaCha20-Poly1305 refuses a changed byte",
			p2p_aead_open(key, nonce, additional, 12, sealed, sealed_size, out) < 0, &failures);
	}

	/* X25519, RFC 7748 section 5.2 */
	{
		unsigned char scalar[32], point[32], result[32];

		hex_bytes("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", scalar, 32);
		hex_bytes("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", point, 32);
		p2p_x25519(result, scalar, point);
		check(report, context, "X25519 (RFC 7748)", same_hex(result, 32,
			"c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552"), &failures);
	}

	/* two machines agree (RFC 7748 section 6.1's keys), and a message goes between them */
	{
		unsigned char a[32], b[32], a_public[32], b_public[32], first[32], second[32];
		int size;

		hex_bytes("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
		hex_bytes("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b, 32);
		p2p_x25519(a_public, a, NULL);
		p2p_x25519(b_public, b, NULL);
		p2p_x25519(first, a, b_public);
		p2p_x25519(second, b, a_public);
		check(report, context, "X25519 public keys (RFC 7748 6.1)", same_hex(a_public, 32,
			"8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a") && same_hex(b_public, 32,
			"de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"), &failures);
		check(report, context, "two machines agree on a secret", !memcmp(first, second, 32) && same_hex(first, 32,
			"4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"), &failures);
		size = p2p_seal(first, "tunnel packet", 13, sealed);
		check(report, context, "a sealed message crosses between them", size == 13 + P2P_SEAL_OVERHEAD &&
			p2p_open(second, sealed, size, out) == 13 && !memcmp(out, "tunnel packet", 13), &failures);
		sealed[size - 1] ^= 0x80;
		check(report, context, "a changed sealed message is refused", p2p_open(second, sealed, size, out) < 0,
			&failures);
	}

	/* Ed25519, RFC 8032 test 1 (an empty message) */
	{
		unsigned char seed[32], pub[32], secret[32], signature[64], x25519_public[32];

		hex_bytes("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32);
		p2p_ed25519_public(seed, pub, secret);
		check(report, context, "Ed25519 public key (RFC 8032)", same_hex(pub, 32,
			"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"), &failures);
		p2p_ed25519_sign(seed, pub, "", 0, signature);
		check(report, context, "Ed25519 signature (RFC 8032)", same_hex(signature, 64,
			"e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46b"
			"d25bf5f0595bbe24655141438e7a100b"), &failures);
		check(report, context, "Ed25519 verifies it", p2p_ed25519_verify(pub, "", 0, signature), &failures);
		signature[10] ^= 1;
		check(report, context, "Ed25519 refuses a changed signature", !p2p_ed25519_verify(pub, "", 0, signature),
			&failures);
		/* (the invite's host part is the hash of the X25519 form of the Ed25519 key) */
		check(report, context, "Ed25519 key to X25519", p2p_ed25519_to_x25519(pub, x25519_public), &failures);
	}

	/* the random numbers the keys and nonces come from */
	{
		unsigned char first[64], second[64];
		int index, bits = 0, zero = 1;

		memset(first, 0, sizeof(first));
		memset(second, 0, sizeof(second));
		posix_random_bytes(first, sizeof(first));
		posix_random_bytes(second, sizeof(second));
		for (index = 0; index < 64; index++)
		{
			unsigned char v = first[index];

			zero &= v == 0;
			while (v)
			{
				bits += v & 1;
				v >>= 1;
			}
		}
		/* (512 bits, half of them set: 4 or more standard deviations off is not chance) */
		check(report, context, "random numbers: not zero, not repeated, bits balanced", !zero &&
			memcmp(first, second, sizeof(first)) && bits > 200 && bits < 312, &failures);
	}
	return failures;
}
