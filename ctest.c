/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2020 Olivier Dion <olivier.dion@polymtl.ca>
 *
 * ctest.c - Simple MT tester.
 *
 * ctest allows you to define tests in different compilation units
 * using a declarative style.  All tests are run in parallel in their
 * own thread and results are collected at the end.
 */

#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <semaphore.h>

#include "./ctest.h"


#define array_size(ARR) (sizeof((ARR))/sizeof((ARR)[0]))


__thread struct ctest *ctest_this_test;
int ctest_errno;

static sem_t finish_sem;

static void do_test(struct ctest *test);
static void *do_test_async(void *test);
static void ls_and_exit(int min_lvl, regex_t *preg);


int ctest_do(int flags, int min_lvl, const char *filter_re)
{
	struct ctest *it;
	regex_t preg;
	int ret;
	size_t cnt;

	/* Compile regex if we need it */
	if (!filter_re) {
		goto no_re;
	}

	ret = regcomp(&preg, filter_re, REG_EXTENDED|REG_ICASE|REG_NOSUB);

	if (0 != ret) {
		ret         = -CTEST_EREGEX;
		ctest_errno = ret;
		goto end;
	}
no_re:
	if (flags & CTEST_DRY_RUN) {
		if (filter_re) {
			ls_and_exit(min_lvl, &preg);
		} else {
			ls_and_exit(min_lvl, NULL);
		}
	}

	if (0 != sem_init(&finish_sem, 0 ,0)) {
		ret         = -CTEST_EPOSIX;
		ctest_errno = errno;
		goto free_re;
	}

	cnt = 0;

	CTEST_FOR_EACH_TEST(it) {

		/* Filter based on level */
		if (it->lvl < min_lvl) {
			it->state = CTEST_SKIP;
			continue;
		}

		/* Filter based on regex */
		if (filter_re) {
			int match = regexec(&preg, it->name, 0, NULL, 0);

			if (REG_NOMATCH == match) {
				it->state = CTEST_SKIP;
				continue;
			}
		}

		do_test(it);
		++cnt;
	}

	/* Wait for all tests to finish */
	while (cnt) {
		sem_wait(&finish_sem);
		--cnt;
	}
	sem_destroy(&finish_sem);

	ret = CTEST_OK;

free_re:
	if (filter_re) {
		regfree(&preg);
	}
end:
	return ret;
}

const char *ctest_state2str(int state)
{
	static const char *state2str[] = {
		[CTEST_PASS]  = "PASSED",
		[CTEST_SKIP]  = "SKIPPED",
		[CTEST_FAIL]  = "FAILED",
		[CTEST_PANIC] = "PANICKED"
	};

	if (state < 0 || state >= (int)array_size(state2str)) {
		return "(invalid state)";
	}
	return state2str[state];
}

static void do_test(struct ctest *test)
{
	pthread_t t;
	int err;

	err = pthread_create(&t, NULL, do_test_async, test);

	if (0 != err) {
		fprintf(stderr, "ctest: pthread_create() failed - %s\n",
			strerror(errno));
		goto bad_posix;
	}

	pthread_detach(t);

bad_posix:
	return;
}

static void *do_test_async(void *test)
{
	int ret;

	ctest_this_test = test;

	ret = setjmp(ctest_this_test->jmp);

	if (0 == ret) {
		(ctest_this_test->func)(ctest_this_test->arg);
		ret = CTEST_PASS;
	}

	ctest_this_test->state = ret;
	sem_post(&finish_sem);

	return NULL;
}

static void ls_and_exit(int min_lvl, regex_t *preg)
{
	struct ctest *it;
	CTEST_FOR_EACH_TEST(it) {



		if (it->lvl < min_lvl) {
			continue;
		}

		if (preg) {
			int match = regexec(preg, it->name, 0, NULL, 0);

			if (REG_NOMATCH == match) {
				it->state = CTEST_SKIP;
				continue;
			}
		}

		printf("%s\n", it->name);
	}

	exit(EXIT_SUCCESS);
}

#ifdef CTEST_SELF_TEST

#define streq(A, B) (0 == strcmp((A), (B)))

static void usage(void)
{
	printf("usage: ctest [-d] [-l LVL] [-r RE]\n"
	       "-d   Dry run\n"
	       "-l   Level threshold\n"
	       "-r   Regex filter\n");
	exit(EXIT_SUCCESS);
}

static void my_test_func(void *data)
{
	int tmp = (long int)data;

	ctest_assert(CTEST_PANIC != tmp);
	ctest_done(tmp);
	printf("NEVER REACHED\n");
}

CTEST_TEST_IS(
	CTEST_TEST_NAME("panicked")
	CTEST_TEST_FUNC(my_test_func)
	CTEST_TEST_ARG((void*)CTEST_PANIC)
	CTEST_TEST_LVL(0)
	);

CTEST_TEST_IS(
	CTEST_TEST_NAME("failed")
	CTEST_TEST_FUNC(my_test_func)
	CTEST_TEST_ARG((void*)CTEST_FAIL)
	CTEST_TEST_LVL(0)
	);

CTEST_TEST_IS(
	CTEST_TEST_NAME("passed")
	CTEST_TEST_FUNC(my_test_func)
	CTEST_TEST_ARG((void*)CTEST_PASS)
	CTEST_TEST_LVL(0)
	);

/*
 * If the skip-* tests are executed, we will get a SEGFAULT because
 * the state will be out of bound
 */
CTEST_TEST_IS(
	CTEST_TEST_NAME("skip-lvl")
	CTEST_TEST_FUNC(my_test_func)
	CTEST_TEST_ARG((void*)-1)
	CTEST_TEST_LVL(-1)
	);

CTEST_TEST_IS(
	CTEST_TEST_NAME("skip-re")
	CTEST_TEST_FUNC(my_test_func)
	CTEST_TEST_ARG((void*)-1)
	CTEST_TEST_LVL(49)
	);

int main(int argc, char *argv[])
{
	struct ctest *it;

	int flags = 0;
	int lvl   = 0;
	char *re  = "passed|failed|panicked|skip-lvl";

	for (int i=1; i<argc; ++i) {
		if (streq(argv[i], "-d")) {
			flags |= CTEST_DRY_RUN;
		} else if (streq(argv[i], "-h")) {
			usage();
		} else if (streq(argv[i], "-l")) {
			if (i + 1 == argc) {
				usage();
			}
			lvl = atoi(argv[i+1]);
			++i;
		} else if (streq(argv[i], "-r")) {
			if (i + 1 == argc) {
				usage();
			}
			re = argv[i+1];
			++i;
		}
	}

	ctest_do(flags, lvl, re);

	printf("SUMMARY:\n");
	CTEST_FOR_EACH_TEST(it) {
		printf("\t%8s: %s\n", ctest_state2str(it->state), it->name);
	}

	return EXIT_SUCCESS;
}
#endif
