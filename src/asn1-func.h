/* asn1-func.h - definitions for asn1-func.c
 *      Copyright (C) 2000,2001 Fabio Fiorina
 *      Copyright (C) 2001 Free Software Foundation, Inc.
 *
 * This file is part of GNUTLS.
 *
 * GNUTLS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GNUTLS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef ASN1_FUNC_H
#define ASN1_FUNC_H

/* Error Codes */
enum {
  ASN_OK                  =  0,
  ASN_FILE_NOT_FOUND      =  1,
  ASN_ELEMENT_NOT_FOUND   =  2,
  ASN_IDENTIFIER_NOT_FOUND=  3,
  ASN_DER_ERROR           =  4,
  ASN_VALUE_NOT_FOUND     =  5,
  ASN_GENERIC_ERROR       =  6,
  ASN_VALUE_NOT_VALID     =  7,
  ASN_TAG_ERROR           =  8,
  ASN_TAG_IMPLICIT        =  9,
  ASN_ERROR_TYPE_ANY      = 10,
  ASN_SYNTAX_ERROR        = 11,
  ASN_MEM_ERROR           = 12
};

/* The values below 128 correspond to the  Universal tag values */
typedef enum {
  TYPE_NONE = 0,
  TYPE_BOOLEAN = 1,
  TYPE_INTEGER = 2,
  TYPE_BIT_STRING = 3,
  TYPE_OCTET_STRING = 4,
  TYPE_NULL = 5,
  TYPE_OBJECT_ID = 6,
  TYPE_OBJECT_DESCRIPTOR = 7,
  TYPE_EXTERNAL = 8,
  TYPE_REAL = 9,
  TYPE_ENUMERATED = 10,
  TYPE_EMBEDDED_PDV = 11,
  TYPE_UTF8_STRING = 12,
  TYPE_REALTIVE_OID = 13,
  TYPE_SEQUENCE = 16,
  TYPE_SET = 17,
  TYPE_NUMERIC_STRING = 18,
  TYPE_PRINTABLE_STRING = 19,
  TYPE_TELETEX_STRING = 20,
  TYPE_VIDEOTEX_STRING = 21,
  TYPE_IA5_STRING = 22,
  TYPE_UTC_TIME = 23,
  TYPE_GENERALIZED_TIME = 24,
  TYPE_GRAPHIC_STRING = 25,
  TYPE_VISIBLE_STRING = 26,
  TYPE_GENERAL_STRING = 27,
  TYPE_UNIVERSAL_STRING = 28,
  TYPE_CHARACTER_STRING = 29,
  TYPE_BMP_STRING = 30,
  /* the following values do not correspond to an Universal tag */
  TYPE_CONSTANT = 128,
  TYPE_IDENTIFIER,
  TYPE_TAG,
  TYPE_DEFAULT,
  TYPE_SIZE,
  TYPE_SEQUENCE_OF,
  TYPE_ANY,
  TYPE_SET_OF,
  TYPE_DEFINITIONS,
  TYPE_CHOICE,
  TYPE_IMPORTS
} node_type_t;


enum tag_class {
  CLASS_UNIVERSAL = 0,
  CLASS_APPLICATION = 1,
  CLASS_CONTEXT = 2,
  CLASS_PRIVATE =3
};

struct node_flag_s {
  enum tag_class class;
  int explicit:1;
  int implicit:1;
  int has_imports:1;
  int assignment:1;  /* node is an assignment */
  int one_param:1;
  int has_tag:1; 
  int has_size:1;
  int has_list:1;
  int has_min_max:1;
  int has_defined_by:1;
  int is_false:1;
  int is_true:1;
  int has_default:1;  /* node has a default value (fixme:needed???)*/
  int is_optional:1;
  int in_set:1;       
  int in_choice:1;
  int in_array:1;
  int not_used:1;     
  int help_down:1;    /* helper for create_tree */
  int help_right:1;   /* helper for create_tree */
  int tag_seen:1;
  int skip_this:1;   /* helper */
};

enum asn_value_type {
  VALTYPE_NULL = 0,
  VALTYPE_BOOL,
  VALTYPE_CSTR,
  VALTYPE_MEM,
  VALTYPE_LONG,
  VALTYPE_ULONG
};

union asn_value_u {
  int v_bool;
  char *v_cstr;
  struct {
    size_t len;
    unsigned char *buf;
  } v_mem;
  long v_long;
  unsigned long v_ulong;
};

/******************************************************/
/* Structure definition used for the node of the tree */
/* that rappresent an ASN.1 DEFINITION.               */
/******************************************************/
typedef struct asn_node_struct *AsnNode; 
struct asn_node_struct {
  char *name;                    /* Node name */
  node_type_t type;   
  struct node_flag_s flags;

  enum asn_value_type valuetype;
  union asn_value_u value;
  int off;                       /* offset of this TLV */
  int nhdr;                      /* length of the header */
  int len;                       /* length part of the TLV */

  AsnNode down;                  /* Pointer to the son node */
  AsnNode right;                 /* Pointer to the brother node */
  AsnNode left;                  /* Pointer to the next list element */ 
  AsnNode link_next;             /* to keep track of all nodes in a tree */
}; 

/* Structure to keep an entire ASN.1 parse tree and associated information */
struct ksba_asn_tree_s {
  AsnNode parse_tree;
  AsnNode node_list;  /* for easier release of all nodes */
  char filename[1];
};


typedef struct static_struct_asn {
  char *name;                    /* Node name */
  node_type_t type;             /* Node type */
  struct node_flag_s flags;
  char *stringvalue;      
} static_asn;




/***************************************/
/*  Functions used by ASN.1 parser     */
/***************************************/
void _ksba_asn_set_value (AsnNode node, enum asn_value_type vtype,
                          const void *value, size_t len);
void _ksba_asn_set_name (AsnNode node, const char *name);
AsnNode _ksba_asn_walk_tree (AsnNode root, AsnNode node);
AsnNode _ksba_asn_walk_tree_up_right (AsnNode root, AsnNode node);
AsnNode _ksba_asn_find_node(AsnNode pointer,const char *name);
int _ksba_asn_check_identifier(AsnNode node);
int _ksba_asn_change_integer_value(AsnNode node);
int _ksba_asn_delete_not_used(AsnNode node);
int _ksba_asn_expand_object_id(AsnNode node);
void _ksba_asn_set_default_tag (AsnNode node);
void _ksba_asn_type_set_config (AsnNode node);
AsnNode _ksba_asn_expand_tree (AsnNode src_root);


/*-- asn1-func.c --*/
void _ksba_asn_node_dump (AsnNode p, FILE *fp);
void _ksba_asn_node_dump_all (AsnNode root, FILE *fp);



int ksba_asn_create_structure (AsnNode root, char *source_name,
                               AsnNode*pointer , char *dest_name);
int ksba_asn_delete_structure (AsnNode root);
int ksba_asn1_create_tree (const static_asn *root,AsnNode*pointer);
int ksba_asn_read_value(AsnNode root,const char *name,unsigned char *value,int *len);
int ksba_asn_write_value(AsnNode root,char *name,unsigned char *value,int len);




#endif /*ASN1_FUNC_H*/

