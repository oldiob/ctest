#ifndef _CTEST_H
#define _CTEST_H


/* Libs */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>


/* Configuration definitions */

/*
 * These are used internally in ctest.c
 */
#ifndef CTEST_MALLOC
#  define CTEST_MALLOC malloc
#endif

#ifndef CTEST_CALLOC
#  define CTEST_CALLOC calloc
#endif

#ifndef CTEST_REALLOC
#  define CTEST_REALLOC realloc
#endif

#ifndef CTEST_FREE
#  define CTEST_FREE free
#endif

/*
 * What to do when an assertion has fail?
 */
#ifndef CTEST_IF_ASSERTION_FAILED
#  define CTEST_IF_ASSERTION_FAILED(EXPR)		\
	ctest_panic("Assertion of \"" #EXPR "\" failed!")
#endif


/*
 * ELF section name where to register ctest's structures.  Make sure
 * that this section contains nothing else or you _will_ get a
 * SEGFAULT
 */
#ifndef CTEST_SECTION
#  define CTEST_SECTION ctest_registration
#endif


/*
 * Forcing correct alignement of the 'struct ctest' put into the
 * CTEST_SECTION.  Otherwise, the iterator won't work.
 */
#if __x86_64__ || __aarch64__ || __powerpc64__ || __sparc__ || __mips__
#  define CTEST_STRUCT_ALIGN 8
#elif __i386__ || __arm__ || __m68k__ || __ILP32__
#  define CTEST_STRUCT_ALIGN 4
#endif



/* Helper macros */
#define CTEST_CAT_PRIMITIVE(X, Y) X ## Y
#define CTEST_CAT(X, Y) CTEST_CAT_PRIMITIVE(X, Y)

#define CTEST_STR_PRIMITIVE(X) #X
#define CTEST_STR(X) CTEST_STR_PRIMITIVE(X)

#define CTEST_MAKE_ID CTEST_CAT(ctest_ID_, __COUNTER__)

#define CTEST_FOR_EACH_TEST(IT)						\
	for (IT=&CTEST_START_SECTION; IT<&CTEST_STOP_SECTION; ++IT)

/* Top definitions */

enum ctest_state {
	CTEST_NIL,
	CTEST_PASS,
	CTEST_SKIP,
	CTEST_FAIL,
	CTEST_PANIC
};

enum ctest_err {
	CTEST_OK,
	CTEST_EINVAL,
	CTEST_EREGEX,
	CTEST_EPOSIX,
};

enum ctest_flags {
	CTEST_DRY_RUN = 0x1
};

struct ctest {
	const char *name;
	void (*func)(void *arg);
	void *arg;
	jmp_buf jmp;
	int lvl;
	int state;
};


/* External linkages */
#define CTEST_START_SECTION CTEST_CAT(__start_, CTEST_SECTION)
#define CTEST_STOP_SECTION  CTEST_CAT(__stop_, CTEST_SECTION)
extern struct ctest CTEST_START_SECTION;
extern struct ctest CTEST_STOP_SECTION;

extern __thread struct ctest *ctest_this_test;
extern int ctest_errno;

extern int ctest_do(int flags, int max_lvl, const char *filter_re);
extern const char *ctest_state2str(int state);


/* Printing */
#define ctest_printf(FMT, ARGS...)					\
	do {								\
		printf("%s: " FMT "\n", ctest_this_test->name ,##ARGS);	\
	} while(0)


/* Longjumps and assertion */
#define ctest_done(STATE)				\
	do {						\
		longjmp(ctest_this_test->jmp, STATE);	\
	} while(0)

#define ctest_fail(ARGS...)					\
	do {							\
		ctest_printf(ARGS);				\
		longjmp(ctest_this_test->jmp, CTEST_FAIL);	\
	} while(0)

#define ctest_panic(ARGS...)					\
	do {							\
		ctest_printf(ARGS);				\
		longjmp(ctest_this_test->jmp, CTEST_PANIC);	\
	} while(0);


/* Memory wrappers */
static inline void *ctest_malloc(size_t size)
{
	void *ptr = malloc(size);
	if (!ptr) {
		ctest_panic("Fail to malloc()\n");
	}
	return ptr;
}

static inline void *ctest_realloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr) {
		ctest_panic("Fail to realloc()\n");
	}
	return ptr;
}

#define ctest_free(ptr) free(ptr)

#define ctest_assert(EXPR)					\
	do {							\
		if (!(EXPR)) {					\
			CTEST_IF_ASSERTION_FAILED(EXPR);	\
		}						\
	} while(0)


/* Registration */
#define CTEST_TEST_NAME(NAME) .name  = NAME,
#define CTEST_TEST_FUNC(FUNC) .func  = FUNC,
#define CTEST_TEST_ARG(ARG)   .arg   = ARG,
#define CTEST_TEST_LVL(LVL)   .lvl   = LVL,

#define CTEST_TEST_IS(...)						\
	static struct ctest CTEST_MAKE_ID				\
	__attribute__((section(CTEST_STR(CTEST_SECTION)), used))	\
	__attribute__((aligned(CTEST_STRUCT_ALIGN)))			\
	= {								\
		.state = CTEST_NIL,					\
		__VA_ARGS__						\
	}

#endif
