#ifndef _SAFE_FUNCS
#define _SAFE_FUNCS

#include <sys/types.h>


#include <string.h>
#include <stdio.h>

/* http://www.rsdn.ru/forum/cpp/2041382.flat.aspx */
#ifdef _MSC_VER
#define __attribute__(tag)
#endif/*_MSC_VER*/

/* sizeof is usually a compile-time operator, which means
that during compilation, sizeof and its operand get replaced
by the result-value. This is evident in the assembly language
code produced by a C or C++ compiler. For this reason, sizeof
qualifies as an operator, even though its use sometimes looks
like a function call. Applying sizeof to variable length arrays,
introduced in C99, is an exception to this rule. */

size_t	 strlcpy(char *, const char *, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));
char	*strsep(char **, const char *);


#endif