#include <ctype.h>
#include <stdlib.h>

#include <openssl/evp.h>

#include "msgdigest.h"

int
gfarm_msgdigest_name_verify(const char *gfarm_name)
{
	const unsigned char *s = (const unsigned char *)gfarm_name;
	const EVP_MD *md_type;

	/*
	 * Gfarm uses just same names with OpenSSL for now,
	 * except that only lower-case characters and digits are allowed.
	 */
	for (; *s != '\0'; s++) {
		if (!islower(*s) && !isdigit(*s))
			return 0;
	}

	md_type = EVP_get_digestbyname(
	    gfarm_msgdigest_name_to_openssl(gfarm_name));

	return (md_type != NULL);
}

const char *
gfarm_msgdigest_name_to_openssl(const char *gfarm_name)
{
	return (gfarm_name); /* XXX just same names with OpenSSL for now */
}

#ifndef __KERNEL__
int
gfarm_msgdigest_init(const char *md_type_name, EVP_MD_CTX *md_ctx,
	int *not_supported_p)
{
	const EVP_MD *md_type;

	if (md_type_name == NULL || md_type_name[0] == '\0') {
		if (not_supported_p != NULL)
			*not_supported_p = 0;
		return (0); /* DO NOT calculate message digest */
	}

	md_type = EVP_get_digestbyname(
	    gfarm_msgdigest_name_to_openssl(md_type_name));
	if (md_type == NULL) {
		if (not_supported_p != NULL)
			*not_supported_p = 1;
		return (0); /* not supported */
	}

	EVP_DigestInit(md_ctx, md_type);
	return (1); /* calculate message digest */
}
#else /* __KERNEL__ */
int
gfarm_msgdigest_init(const char *md_type_name, EVP_MD_CTX *md_ctx,
	int *not_supported_p)
{
	if (not_supported_p != NULL)
		*not_supported_p = 1;
	return (0); /* not supported */
}
#endif /* __KERNEL__ */

/*
 * md_string should be declared as:
 * 	char md_string[GFARM_MSGDIGEST_STRSIZE];
 */
size_t
gfarm_msgdigest_to_string(
	char *md_string, unsigned char *md_value, size_t md_len)
{
	size_t i;

	for (i = 0; i < md_len; ++i)
		sprintf(&md_string[i * 2], "%02x", md_value[i]);
	return (md_len * 2);
}

size_t
gfarm_msgdigest_final(unsigned char *md_value, EVP_MD_CTX *md_ctx)
{
	unsigned int md_len;

	EVP_DigestFinal(md_ctx, md_value, &md_len);
	return (md_len);
}

size_t
gfarm_msgdigest_final_string(char *md_string, EVP_MD_CTX *md_ctx)
{
	size_t md_len;
	unsigned char md_value[EVP_MAX_MD_SIZE];

	md_len = gfarm_msgdigest_final(md_value, md_ctx);
	return (gfarm_msgdigest_to_string(md_string, md_value, md_len));
}
