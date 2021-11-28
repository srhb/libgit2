/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include <git2.h>
#include "cli.h"
#include "cmd.h"

#define COMMAND_NAME "cat-file"

typedef enum {
	DISPLAY_CONTENT = 0,
	DISPLAY_EXISTS,
	DISPLAY_PRETTY,
	DISPLAY_SIZE,
	DISPLAY_TYPE
} display_t;

static int show_help;
static int display = DISPLAY_CONTENT;
static char *type_name, *object_spec;

static const cli_opt_spec opts[] = {
	{ CLI_OPT_TYPE_SWITCH,    "help",    0, &show_help,   1,
	  CLI_OPT_USAGE_HIDDEN | CLI_OPT_USAGE_STOP_PARSING, NULL,
	  "display help about the " COMMAND_NAME " command" },

	{ CLI_OPT_TYPE_SWITCH,     NULL,    't', &display,    DISPLAY_TYPE,
	  CLI_OPT_USAGE_REQUIRED,  NULL,    "display the type of the object" },
	{ CLI_OPT_TYPE_SWITCH,     NULL,    's', &display,    DISPLAY_SIZE,
	  CLI_OPT_USAGE_CHOICE,    NULL,    "display the size of the object" },
	{ CLI_OPT_TYPE_SWITCH,     NULL,    'e', &display,    DISPLAY_EXISTS,
	  CLI_OPT_USAGE_CHOICE,    NULL,    "displays nothing unless the object is corrupt" },
	{ CLI_OPT_TYPE_SWITCH,     NULL,    'p', &display,    DISPLAY_PRETTY,
	  CLI_OPT_USAGE_CHOICE,    NULL,    "pretty-print the object" },
	{ CLI_OPT_TYPE_ARG,       "type",    0, &type_name,   0,
	  CLI_OPT_USAGE_CHOICE,   "type",   "the type of object to display" },
	{ CLI_OPT_TYPE_ARG,       "object",  0, &object_spec, 0,
	  CLI_OPT_USAGE_REQUIRED, "object", "the object to display" },
	{ 0 },
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts);
	printf("\n");

	printf("Display the content for the given object in the repository.\n");
	printf("\n");

	printf("Options:\n");

	cli_opt_help_fprint(stdout, opts);
}

static int print_odb(git_object *object, display_t display)
{
	git_odb *odb = NULL;
	git_odb_object *odb_object = NULL;
	const unsigned char *content;
	git_object_size_t size;
	int error = 0;

	/*
	 * Our parsed blobs retain the raw content; all other objects are
	 * parsed into a working representation.  To get the raw content,
	 * we need to do an ODB lookup.  (Thankfully, this should be cached
	 * in-memory from our last call.)
	 */
	if (git_object_type(object) == GIT_OBJECT_BLOB) {
		content = git_blob_rawcontent((git_blob *)object);
		size = git_blob_rawsize((git_blob *)object);
	} else {
		if ((error = git_repository_odb(&odb, git_object_owner(object))) < 0 ||
		    (error = git_odb_read(&odb_object, odb, git_object_id(object))) < 0) {
			fprintf(stderr, "%s: %s\n", PROGRAM_NAME, git_error_last()->message);
			error = 1;
			goto done;
		}

		content = git_odb_object_data(odb_object);
		size = git_odb_object_size(odb_object);
	}

	switch (display) {
	case DISPLAY_SIZE:
		error = printf("%" PRIu64 "\n", size);
		break;
	case DISPLAY_CONTENT:
		error = p_write(fileno(stdout), content, (size_t)size);
		break;
	default:
		abort();
	}

	if (error < 0)
		perror(PROGRAM_NAME);

done:
	git_odb_object_free(odb_object);
	git_odb_free(odb);
	return error < 0 ? 1 : 0;
}

static int print_type(git_object *object)
{
	int error;

	if ((error = printf("%s\n", git_object_type2string(git_object_type(object)))) < 0)
		fprintf(stderr, "%s: %s\n", PROGRAM_NAME, strerror(errno));

	return error < 0 ? 1 : 0;
}

static int print_pretty(git_object *object)
{
	const git_tree_entry *entry;
	size_t i, count;

	/*
	 * Only trees are stored in an unreadable format and benefit from
	 * pretty-printing.
	 */
	if (git_object_type(object) != GIT_OBJECT_TREE)
		return print_odb(object, DISPLAY_CONTENT);

	for (i = 0, count = git_tree_entrycount((git_tree *)object); i < count; i++) {
		entry = git_tree_entry_byindex((git_tree *)object, i);

		printf("%06o %s %s\t%s\n",
			git_tree_entry_filemode_raw(entry),
			git_object_type2string(git_tree_entry_type(entry)),
			git_oid_tostr_s(git_tree_entry_id(entry)),
			git_tree_entry_name(entry));
	}

	return 0;
}

int cmd_cat_file(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_object *object = NULL;
	git_object_t type;
	cli_opt invalid_opt;
	int error, ret = 1;

	if (cli_opt_parse(&invalid_opt, opts, argv + 1, argc - 1))
		return cli_opt_usage_error(COMMAND_NAME, opts, &invalid_opt);

	if (show_help) {
		print_help();
		return 0;
	}

	if (git_repository_open_ext(&repo, ".", GIT_REPOSITORY_OPEN_FROM_ENV, NULL) < 0) {
		printf("%s: %s\n", PROGRAM_NAME, git_error_last()->message);
		return 128;
	}

	if ((error = git_revparse_single(&object, repo, object_spec)) < 0) {
		if (display == DISPLAY_EXISTS && error == GIT_ENOTFOUND) {
			ret = 1;
		} else {
			printf("%s: %s\n", PROGRAM_NAME, git_error_last()->message);
			ret = 128;
		}

		goto done;
	}

	if (type_name) {
		git_object *peeled;

		if ((type = git_object_string2type(type_name)) == GIT_OBJECT_INVALID) {
			fprintf(stderr, "%s: invalid object type '%s'\n", PROGRAM_NAME, type_name);
			return 129;
		}

		if (git_object_peel(&peeled, object, type) < 0) {
			fprintf(stderr, "%s: %s\n", PROGRAM_NAME, git_error_last()->message);
			goto done;
		}

		git_object_free(object);
		object = peeled;
	}

	switch (display) {
	case DISPLAY_EXISTS:
		ret = 0;
		break;
	case DISPLAY_TYPE:
		ret = print_type(object);
		break;
	case DISPLAY_PRETTY:
		ret = print_pretty(object);
		break;
	default:
		ret = print_odb(object, display);
		break;
	}

done:
	git_object_free(object);
	git_repository_free(repo);
	return ret;
}
