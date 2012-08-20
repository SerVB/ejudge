/* -*- c -*- */
/* $Id$ */

#ifndef __XML_UTILS_H__
#define __XML_UTILS_H__

/* Copyright (C) 2004-2012 Alexander Chernov <cher@ejudge.ru> */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "ej_types.h"

#include <stdio.h>
#include <time.h>

struct xml_tree;
struct xml_attr;
struct xml_parse_spec;

int xml_parse_ip(FILE *log_f, unsigned char const *path, int line, int column,
                 unsigned char const *s, ej_ip_t *pip);
int xml_parse_date(FILE *log_f, unsigned char const *path, int line, int column,
                   unsigned char const *s, time_t *pd);
int xml_parse_int(FILE *log_f, unsigned char const *path, int line, int column,
                  unsigned char const *str, int *pval);
int xml_parse_ip_mask(FILE *log_f, const unsigned char *path, int line, int column,
                      const unsigned char *s,
                      ej_ip_t *p_ip, ej_ip_t *p_mask);
int xml_parse_bool(FILE *log_f, unsigned char const *path, int line, int column,
                   unsigned char const *s, int *pv);

void xml_unparse_text(FILE *f, const unsigned char *tag_name,
                      unsigned char const *value,
                      unsigned char const *indent);

const unsigned char *xml_unparse_bool(int b);
const unsigned char *xml_unparse_ip(ej_ip_t ip);
const unsigned char *xml_unparse_date(time_t d);
const unsigned char *xml_unparse_ip_mask(ej_ip_t addr, ej_ip_t mask);

extern const unsigned char *xml_err_path;
extern const struct xml_parse_spec *xml_err_spec;
extern FILE *xml_err_file;

const unsigned char * xml_err_get_elem_name(const struct xml_tree *p);
const unsigned char * xml_err_get_attr_name(const struct xml_attr *a);

void xml_err(const struct xml_tree *pos, const char *format, ...)
     __attribute__((format (printf, 2, 3)));
void xml_err_a(const struct xml_attr *pos, const char *format, ...)
     __attribute__((format (printf, 2, 3)));
int xml_err_attrs(const struct xml_tree *p);
int xml_err_nested_elems(const struct xml_tree *p);
int xml_err_attr_not_allowed(const struct xml_tree *tree,
                             const struct xml_attr *attr);
int xml_err_elem_not_allowed(const struct xml_tree *tree);
int xml_err_elem_redefined(const struct xml_tree *tree);
int xml_err_top_level(const struct xml_tree *tree, int elem);
int xml_err_top_level_s(const struct xml_tree *, const unsigned char *);
int xml_err_attr_invalid(const struct xml_attr *a);
int xml_err_elem_undefined(const struct xml_tree *p, int elem);
int xml_err_elem_undefined_s(const struct xml_tree *p, const unsigned char *);
int xml_err_attr_undefined(const struct xml_tree *p, int attr);
int xml_err_attr_undefined_s(const struct xml_tree *p, const unsigned char *);
int xml_err_elem_invalid(const struct xml_tree *p);
int xml_err_elem_empty(const struct xml_tree *p);

int xml_leaf_elem(struct xml_tree *tree, /* ->text may be modified */
                  unsigned char **value_addr,
                  int move_flag, int empty_allowed_flag);
int xml_empty_text(struct xml_tree *tree);
int xml_empty_text_c(const struct xml_tree *tree);

int xml_attr_bool(const struct xml_attr *attr, int *value_ptr);
int xml_attr_bool_byte(struct xml_attr *attr, unsigned char *value_ptr);
int xml_attr_int(struct xml_attr *attr, int *value_ptr);
int xml_attr_ulong(struct xml_attr *attr, unsigned long *value_ptr);
int xml_attr_date(struct xml_attr *attr, time_t *value_ptr);
int xml_elem_ip_mask(struct xml_tree *tree,
                     unsigned int *addr_ptr, unsigned int *mask_ptr);

int
xml_parse_ip6(
        FILE *log_f,
        unsigned char const *path,
        int line,
        int column,
        unsigned char const *s,
        ej_ip6_t *pip);

#endif /* __XML_UTILS_H__ */

/*
 * Local variables:
 *  compile-command: "make"
 * End:
 */
