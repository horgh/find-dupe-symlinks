//
// This program is to look for symlinks which point to the same target. We start
// from the current working directory.
//
// The reason is I have a set of directories which contain many symlinks, and I
// suspect I have some that are duplicate (due to categorizing). I want to back
// up these directories, and I want to de-reference the symlinks when I do.
// However I also do not want to back up the same data multiple times.
//
// I am writing this in C for practice.
//

// __GNU_SOURCE for asprintf().
#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct __symlink;

struct __symlink {
	char * link_path;
	char * target_path;
	struct __symlink * next;
};

struct __args {
	char * start_dir;
	bool verbose;
};

static struct __args *
__get_args(int, char * *);
static void
__destroy_args(struct __args *);
static struct __symlink *
__find_symlinks(const char * const, const bool);
static char *
__canonicalize_path(const char * const);
static bool
__symlink_exists(const struct __symlink * const,
		const char * const);
static void
__destroy_symlinks(struct __symlink * const);
static struct __symlink *
__append_symlinks(struct __symlink *, struct __symlink *);

int main(int argc, char * * argv)
{
	struct __args * args = __get_args(argc, argv);
	if (!args) {
		printf("Usage: %s <arguments>\n", argv[0]);
		printf("\n");
		printf("  -d <directory>   The directory to look in.\n");
		printf("\n");
		printf("  [-v]             Enable verbose output.\n");
		printf("\n");
		return 1;
	}

	struct __symlink * links = __find_symlinks(args->start_dir, args->verbose);
	if (!links) {
		printf("No links found\n");
		__destroy_args(args);
		return 1;
	}

	__destroy_args(args);

	const struct __symlink * link_ptr = links;
	// Must have a next pointer. We look at the current node's path and compare it
	// with all those following it.
	// Yes, O(n^2).
	while (link_ptr->next) {
		if (__symlink_exists(link_ptr->next, link_ptr->target_path)) {
			printf("Duplicate symlink found: %s and %s both link to %s\n",
					link_ptr->link_path, link_ptr->next->link_path,
					link_ptr->target_path);
		}

		link_ptr = link_ptr->next;
	}

	__destroy_symlinks(links);
}

static struct __args *
__get_args(int argc, char * * argv)
{
	struct __args * args = calloc(1, sizeof(struct __args));
	if (!args) {
		printf("__get_args: %s\n", strerror(ENOMEM));
		return NULL;
	}

	args->start_dir = NULL;
	args->verbose = false;

	int opt = 0;
	while ((opt = getopt(argc, argv, "d:v")) != -1) {
		switch (opt) {
			case 'd':
				if (!optarg || strlen(optarg) == 0) {
					printf("You must provide a parameter to -d\n");
					__destroy_args(args);
					return NULL;
				}

				if (args->start_dir) {
					printf("You specified -d twice.\n");
					__destroy_args(args);
					return NULL;
				}

				args->start_dir = strdup(optarg);
				if (!args->start_dir) {
					printf("__get_args: strdup: %s\n", strerror(errno));
					__destroy_args(args);
					return NULL;
				}
				break;
			case 'v':
				args->verbose = true;
				break;
			default:
				printf("__get_args: Unknown flag: %c\n", opt);
				__destroy_args(args);
				break;
		}
	}

	if (!args->start_dir) {
		printf("You must specify a directory to start in (-d).\n");
		__destroy_args(args);
		return NULL;
	}

	return args;
}

static void
__destroy_args(struct __args * args)
{
	if (!args) {
		return;
	}

	free(args->start_dir);
	free(args);
}

static struct __symlink *
__find_symlinks(const char * const dir_path, const bool verbose)
{
	if (!dir_path || strlen(dir_path) == 0) {
		printf("__find_symlinks: %s\n", strerror(EINVAL));
		return NULL;
	}

	if (verbose) {
		printf("Opening directory %s\n", dir_path);
	}

	DIR * const dh = opendir(dir_path);
	if (!dh) {
		printf("__find_symlinks: opendir(%s): %s\n", dir_path, strerror(errno));
		return NULL;
	}

	struct __symlink * links = NULL;

	while (1) {
		// To error check readdir() we need to be able to check whether errno
		// changed.
		errno = 0;

		const struct dirent * de = readdir(dh);
		if (!de) {
			if (errno != 0) {
				closedir(dh);
				__destroy_symlinks(links);
				printf("__find_symlinks: readdir %s: %s\n", dir_path, strerror(errno));
				return NULL;
			}

			break;
		}

		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
			continue;
		}

		char * full_path = NULL;
		if (asprintf(&full_path, "%s/%s", dir_path, de->d_name) == -1) {
				closedir(dh);
				__destroy_symlinks(links);
				printf("__find_symlinks: asprintf: %s: %s\n", dir_path, de->d_name);
				return NULL;
		}

		// If we have a regular file, go to the next file.
		// If we have a symlink, check where it points to, and record it.
		// If we have a directory, descend into it and find its links.
		// Otherwise raise an error and deal with it.

		struct stat sbuf;
		memset(&sbuf, 0, sizeof(struct stat));
		if (lstat(full_path, &sbuf) != 0) {
				printf("__find_symlinks: stat: %s: %s\n", full_path, strerror(errno));
				closedir(dh);
				__destroy_symlinks(links);
				free(full_path);
				return NULL;
		}

		if (S_ISREG(sbuf.st_mode)) {
			free(full_path);
			continue;
		}

		if (S_ISLNK(sbuf.st_mode)) {
			if (verbose) {
				printf("Symbolic link: %s\n", full_path);
			}

			const size_t bufsize = PATH_MAX+1;
			char * target_path = calloc(bufsize, sizeof(char));
			if (!target_path) {
				printf("__find_symlinks: calloc: %s\n", strerror(errno));
				closedir(dh);
				__destroy_symlinks(links);
				free(full_path);
				return NULL;
			}

			if (readlink(full_path, target_path, bufsize) == -1) {
				printf("__find_symlinks: readlink(%s): %s\n", full_path,
						strerror(errno));
				closedir(dh);
				__destroy_symlinks(links);
				free(full_path);
				free(target_path);
				return NULL;
			}

			char * target_path_canonical = __canonicalize_path(target_path);
			if (!target_path_canonical) {
				printf("__find_symlinks: Unable to canonicalize %s\n", target_path);
				closedir(dh);
				__destroy_symlinks(links);
				free(full_path);
				free(target_path);
				return NULL;
			}

			free(target_path);
			target_path = NULL;

			if (verbose) {
				printf("%s links to %s\n", full_path, target_path_canonical);
			}

			struct __symlink * new_link = calloc(1, sizeof(struct __symlink));
			if (!new_link) {
				printf("__find_symlinks: calloc: %s\n", strerror(errno));
				closedir(dh);
				__destroy_symlinks(links);
				free(full_path);
				free(target_path_canonical);
				return NULL;
			}

			new_link->link_path = full_path;
			new_link->target_path = target_path_canonical;
			new_link->next = NULL;

			links = __append_symlinks(links, new_link);

			continue;
		}

		if (S_ISDIR(sbuf.st_mode)) {
			struct __symlink * const dir_links = __find_symlinks(full_path, verbose);
			if (dir_links) {
				links = __append_symlinks(links, dir_links);
			}
			free(full_path);
			continue;
		}

		printf("__find_symlinks: Unhandled file type: %s\n", full_path);
		closedir(dh);
		__destroy_symlinks(links);
		free(full_path);
		return NULL;
	}

	if (closedir(dh) != 0) {
		__destroy_symlinks(links);
		printf("__find_symlinks: closedir: %s: %s\n", dir_path, strerror(errno));
		return NULL;
	}

	return links;
}

// Collapse multiple / into one. Drop any trailing /.
static char *
__canonicalize_path(const char * const path)
{
	if (!path || strlen(path) == 0) {
		printf("__canonicalize_path: %s\n", strerror(EINVAL));
		return NULL;
	}

	char * new_path = calloc(strlen(path)+1, sizeof(char));
	if (!new_path) {
		printf("__canonicalize_path: %s\n", strerror(errno));
		return NULL;
	}

	size_t pos = 0;
	const char * c_ptr = path;
	while (*c_ptr) {
		if (*c_ptr != '/') {
			new_path[pos] = *c_ptr;
			pos++;
			c_ptr++;
			continue;
		}

		if (pos == 0) {
			new_path[pos] = *c_ptr;
			pos++;
			c_ptr++;
			continue;
		}

		if (new_path[pos-1] == '/') {
			c_ptr++;
			continue;
		}

		new_path[pos] = *c_ptr;
		pos++;
		c_ptr++;
	}

	pos = strlen(new_path)-1;
	while (pos != 0 && new_path[pos] == '/') {
		new_path[pos] = '\0';
		pos--;
	}

	return new_path;
}

static void
__destroy_symlinks(struct __symlink * const links)
{
	if (!links) {
		return;
	}

	struct __symlink * link_ptr = links;
	struct __symlink * next_ptr = NULL;
	while (link_ptr) {
		next_ptr = link_ptr->next;

		free(link_ptr->link_path);
		free(link_ptr->target_path);
		free(link_ptr);

		link_ptr = next_ptr;
	}
}

static struct __symlink *
__append_symlinks(struct __symlink * target, struct __symlink * source)
{
	if (!source) {
		printf("__append_symlinks: %s\n", strerror(EINVAL));
		return NULL;
	}

	if (!target) {
		return source;
	}

	struct __symlink * link_ptr = target;
	while (link_ptr) {
		if (!link_ptr->next) {
			break;
		}

		link_ptr = link_ptr->next;
	}

	link_ptr->next = source;

	return target;
}

// Check if there is a symlink in links with the given target path.
static bool
__symlink_exists(const struct __symlink * const links,
		const char * const target_path)
{
	if (!links) {
		return false;
	}

	if (!target_path || strlen(target_path) == 0) {
		printf("__symlink_exists: %s\n", strerror(EINVAL));
		return false;
	}

	const struct __symlink * link_ptr = links;

	while (link_ptr) {
		if (strcmp(link_ptr->target_path, target_path) == 0) {
			return true;
		}

		link_ptr = link_ptr->next;
	}

	return false;
}
