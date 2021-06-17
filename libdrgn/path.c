// Copyright (c) Facebook, Inc. and its affiliates.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dwarf.h>
#include <stdbool.h>
#include <string.h>

#include "path.h"

bool path_iterator_next(struct path_iterator *it, const char **component,
			size_t *component_len)
{
	while (it->num_components) {
		struct string *cur = &it->components[it->num_components - 1];
		if (!cur->len) {
			it->num_components--;
			continue;
		}

		if (cur->str[cur->len - 1] == '/') {
			if (cur->len == 1) {
				/*
				 * This is an absolute path. Emit an empty
				 * component. Components joined before this one
				 * and remaining ".." components are not
				 * meaningful.
				 */
				it->num_components = 0;
				it->dot_dot = 0;
				*component = "";
				*component_len = 0;
				return true;
			}
			/* Skip redundant slashes. */
			cur->len--;
			continue;
		}

		/* Skip "." components. */
		if (cur->len == 1 && cur->str[0] == '.') {
			it->num_components--;
			continue;
		}
		if (cur->len >= 2 && cur->str[cur->len - 2] == '/' &&
		    cur->str[cur->len - 1] == '.') {
			cur->len -= 1;
			continue;
		}

		/* Count ".." components. */
		if (cur->len == 2 && cur->str[0] == '.' && cur->str[1] == '.') {
			it->num_components--;
			it->dot_dot++;
			continue;
		}
		if (cur->len >= 3 && cur->str[cur->len - 3] == '/' &&
		    cur->str[cur->len - 2] == '.' &&
		    cur->str[cur->len - 1] == '.') {
			cur->len -= 2;
			it->dot_dot++;
			continue;
		}

		/* Emit or skip other components. */
		*component_len = 0;
		while (cur->str[cur->len - 1] != '/') {
			cur->len--;
			(*component_len)++;
			if (!cur->len)
				break;
		}
		if (it->dot_dot) {
			it->dot_dot--;
			continue;
		}

		*component = &cur->str[cur->len];
		return true;
	}

	if (it->dot_dot) {
		/*
		 * Leftover ".." components must be above the current
		 * directory.
		 */
		it->dot_dot--;
		*component = "..";
		*component_len = 2;
		return true;
	}
	return false;
}

bool path_ends_with(struct path_iterator *haystack,
		    struct path_iterator *needle)
{
	for (;;) {
		const char *h_component, *n_component;
		size_t h_component_len, n_component_len;

		if (!path_iterator_next(needle, &n_component, &n_component_len))
			return true;

		if (!path_iterator_next(haystack, &h_component,
					&h_component_len))
			return false;

		if (h_component_len != n_component_len ||
		    memcmp(h_component, n_component, h_component_len) != 0)
			return false;
	}
}

bool die_matches_filename(Dwarf_Die *die, const char *filename)
{
	if (!filename || !filename[0])
		return true;

	struct string die_components[2];
	struct path_iterator die_path = {
		.components = die_components,
	};

	Dwarf_Die cu_die;
	Dwarf_Attribute attr_mem, *attr;
	attr = dwarf_attr_integrate(dwarf_diecu(die, &cu_die, NULL, NULL),
				    DW_AT_comp_dir, &attr_mem);
	const char *path = dwarf_formstring(attr);
	if (path) {
		die_path.components[die_path.num_components].str = path;
		die_path.components[die_path.num_components].len = strlen(path);
		die_path.num_components++;
	}

	path = dwarf_decl_file(die);
	if (!path)
		return false;
	/*
	 * If the declaration file name is absolute, the compilation directory
	 * component will be ignored.
	 */
	die_path.components[die_path.num_components].str = path;
	die_path.components[die_path.num_components].len = strlen(path);
	die_path.num_components++;

	struct path_iterator needle = {
		.components = (struct string []){
			{ filename, strlen(filename) }
		},
		.num_components = 1,
	};

	return path_ends_with(&die_path, &needle);
}

LIBDRGN_PUBLIC bool drgn_filename_matches(const char *haystack,
					  const char *needle)
{
	struct path_iterator haystack_path = {
		.components = (struct string []){
			{ haystack, strlen(haystack) }
		},
		.num_components = 1,
	};
	struct path_iterator needle_path = {
		.components = (struct string []){
			{ needle, strlen(needle) }
		},
		.num_components = 1,
	};
	return path_ends_with(&haystack_path, &needle_path);
}
