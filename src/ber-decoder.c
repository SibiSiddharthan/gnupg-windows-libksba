/* ber-decoder.c - Basic Encoding Rules Decoder
 *      Copyright (C) 2001 g10 Code GmbH
 *
 * This file is part of KSBA.
 *
 * KSBA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * KSBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "util.h"

#include "ksba.h"
#include "asn1-func.h"
#include "ber-decoder.h"



struct tag_info {
  enum tag_class class;
  int is_constructed;
  unsigned long tag;
  unsigned long length; /* length part of the TLV */
  int ndef;             /* It is an indefinite length */
  size_t nhdr;          /* number of bytes in the TL */
  unsigned char buf[10]; /* buffer for the TL */
};


struct decoder_state_item_s {
  AsnNode node;
  int went_up;
  int in_seq_of;
  int again;
  int next_tag;
  int length;  /* length of the value */
  int ndef_length; /* the length is of indefinite length */
  int nread;   /* number of value bytes processed */
};
typedef struct decoder_state_item_s DECODER_STATE_ITEM;

struct decoder_state_s {
  DECODER_STATE_ITEM cur;     /* current state */
  int stacksize;
  int idx;
  DECODER_STATE_ITEM stack[1];
};
typedef struct decoder_state_s *DECODER_STATE;


struct ber_decoder_s {
  AsnNode module;    /* the ASN.1 structure */
  KsbaReader reader;
  const char *last_errdesc; /* string with the error description */
  int non_der;    /* set if the encoding is not DER conform */
  AsnNode root;   /* of the expanded parse tree */
  DECODER_STATE ds;
  int bypass;
  int debug;
  int use_image;
  struct {
    unsigned char *buf;
    size_t used;
    size_t length;
  } image;
  struct {
    int primitive;  /* current value is a primitive one */
    int length;     /* length of the primitive one */
    int nhdr;       /* length of the header */
    AsnNode node;   /* NULL or matching node */
  } val; 
};




static DECODER_STATE
new_decoder_state (void)
{
  DECODER_STATE ds;

  ds = xmalloc (sizeof (*ds) + 99*sizeof(DECODER_STATE_ITEM));
  ds->stacksize = 100;
  ds->idx = 0;
  ds->cur.node = NULL;
  ds->cur.in_seq_of = 0;
  ds->cur.again = 0;
  ds->cur.next_tag = 0;
  ds->cur.went_up = 0;
  ds->cur.length = 0;
  ds->cur.ndef_length = 1;
  ds->cur.nread = 0;
  return ds;
}
       
static void        
release_decoder_state (DECODER_STATE ds)
{
  xfree (ds);
}

static void
dump_decoder_state (DECODER_STATE ds)
{
  int i;

  for (i=0; i < ds->idx; i++)
    {
      fprintf (stdout,"  ds stack[%d] (", i);
      if (ds->stack[i].node)
        _ksba_asn_node_dump (ds->stack[i].node, stdout);
      else
        printf ("Null");
      fprintf (stdout,") %s%d (%d)%s\n",
               ds->stack[i].ndef_length? "ndef ":"",
               ds->stack[i].length,
               ds->stack[i].nread,
               ds->stack[i].in_seq_of? " in_seq_of":"");
    }
}

/* Push ITEM onto the stack */
static void
push_decoder_state (DECODER_STATE ds)
{
  if (ds->idx >= ds->stacksize)
    {
      fprintf (stderr, "ERROR: decoder stack overflow!\n");
      abort ();
    }
  ds->stack[ds->idx++] = ds->cur;
}

static void
pop_decoder_state (DECODER_STATE ds)
{
  if (!ds->idx)
    {
      fprintf (stderr, "ERROR: decoder stack underflow!\n");
      abort ();
    }
  ds->cur = ds->stack[--ds->idx];
}



static int
set_error (BerDecoder d, AsnNode node, const char *text)
{
  fprintf (stderr,"ber-decoder: node `%s': %s\n", 
           node? node->name:"?", text);
  d->last_errdesc = text;
  return KSBA_BER_Error;
}


static int
eof_or_error (BerDecoder d, int premature)
{
  if (ksba_reader_error (d->reader))
    {
      set_error (d, NULL, "read error");
      return KSBA_Read_Error;
    }
  if (premature)
    return set_error (d, NULL, "premature EOF");
  return -1;
}

static int
is_primitive_type (node_type_t type)
{
  switch (type)
    {
    case TYPE_BOOLEAN:                               
    case TYPE_INTEGER:                               
    case TYPE_BIT_STRING:                            
    case TYPE_OCTET_STRING:                          
    case TYPE_NULL:                                  
    case TYPE_OBJECT_ID:                             
    case TYPE_OBJECT_DESCRIPTOR:                     
    case TYPE_REAL:                                  
    case TYPE_ENUMERATED:                            
    case TYPE_UTF8_STRING:                           
    case TYPE_REALTIVE_OID:                          
    case TYPE_NUMERIC_STRING:                        
    case TYPE_PRINTABLE_STRING:                      
    case TYPE_TELETEX_STRING:                        
    case TYPE_VIDEOTEX_STRING:                       
    case TYPE_IA5_STRING:                            
    case TYPE_UTC_TIME:                              
    case TYPE_GENERALIZED_TIME:                      
    case TYPE_GRAPHIC_STRING:                        
    case TYPE_VISIBLE_STRING:                        
    case TYPE_GENERAL_STRING:                        
    case TYPE_UNIVERSAL_STRING:                      
    case TYPE_CHARACTER_STRING:                      
    case TYPE_BMP_STRING:                            
      return 1;
    default:
      return 0;
    }
}

static const char *
universal_tag_name (unsigned long no)
{
  static const char *names[31] = {
    "[End Tag]",
    "BOOLEAN",
    "INTEGER",
    "BIT STRING",
    "OCTECT STRING",
    "NULL",
    "OBJECT IDENTIFIER",
    "ObjectDescriptor",
    "EXTERNAL",
    "REAL",
    "ENUMERATED",
    "EMBEDDED PDV",
    "UTF8String",
    "RELATIVE-OID",
    "[UNIVERSAL 14]",
    "[UNIVERSAL 15]",
    "SEQUENCE",
    "SET",
    "NumericString",
    "PrintableString",
    "TeletexString",
    "VideotexString",
    "IA5String",
    "UTCTime",
    "GeneralizedTime",
    "GraphicString",
    "VisibleString",
    "GeneralString",
    "UniversalString",
    "CHARACTER STRING",
    "BMPString"
  };

  return no < DIM(names)? names[no]:NULL;
}


static void
dump_tlv (const struct tag_info *ti, FILE *fp)
{
  const char *tagname = NULL;

  if (ti->class == CLASS_UNIVERSAL)
    tagname = universal_tag_name (ti->tag);

  if (tagname)
    fputs (tagname, fp);
  else
    fprintf (fp, "[%s %lu]", 
             ti->class == CLASS_UNIVERSAL? "UNIVERSAL" :
             ti->class == CLASS_APPLICATION? "APPLICATION" :
             ti->class == CLASS_CONTEXT? "CONTEXT-SPECIFIC" : "PRIVATE",
             ti->tag);
  fprintf (fp, " %c hdr=%u len=", ti->is_constructed? 'c':'p', ti->nhdr);
  if (ti->ndef)
    fputs ("ndef", fp);
  else
    fprintf (fp, "%lu", ti->length);
}


static void
clear_help_flags (AsnNode node)
{
  AsnNode p;

  for (p=node; p; p = _ksba_asn_walk_tree (node, p))
    {
      if (p->type == TYPE_TAG)
        {
          p->flags.tag_seen = 0;
        }
      p->flags.skip_this = 0;
    }
  
}



BerDecoder
_ksba_ber_decoder_new (void)
{
  BerDecoder d;

  d = xtrycalloc (1, sizeof *d);
  if (!d)
    return NULL;

  return d;
}

void
_ksba_ber_decoder_release (BerDecoder d)
{
  xfree (d);
}

/**
 * _ksba_ber_decoder_set_module:
 * @d: Decoder object 
 * @module: ASN.1 Parse tree
 * 
 * Initialize the decoder with the ASN.1 module.  Note, that this is a
 * shallow copy of the module.  FIXME: What about ref-counting of
 * AsnNodes?
 * 
 * Return value: 0 on success or an error code
 **/
KsbaError
_ksba_ber_decoder_set_module (BerDecoder d, KsbaAsnTree module)
{
  if (!d || !module)
    return KSBA_Invalid_Value;
  if (d->module)
    return KSBA_Conflict; /* module already set */

  d->module = module->parse_tree;
  return 0;
}


KsbaError
_ksba_ber_decoder_set_reader (BerDecoder d, KsbaReader r)
{
  if (!d || !r)
    return KSBA_Invalid_Value;
  if (d->reader)
    return KSBA_Conflict; /* reader already set */
  
  d->reader = r;
  return 0;
}


/**********************************************
 ***********  decoding machinery  *************
 **********************************************/

static int
read_byte (KsbaReader reader)
{
  unsigned char buf;
  size_t nread;
  int rc;

  do
    rc = ksba_reader_read (reader, &buf, 1, &nread);
  while (!rc && !nread);
  return rc? -1: buf;
}

/*
 * Read the tag and the length part from the TLV triplet. 
 */
static KsbaError
read_tl (BerDecoder d, struct tag_info *ti)
{
  int c;
  unsigned long tag;

  ti->length = 0;
  ti->ndef = 0;
  ti->nhdr = 0;
  /* Get the tag */
  c = read_byte (d->reader);
  if (c==-1)
    return eof_or_error (d, 0);
  ti->buf[ti->nhdr++] = c;
  ti->class = (c & 0xc0) >> 6;
  ti->is_constructed = !!(c & 0x20);
  tag = c & 0x1f;
  if (tag == 0x1f)
    {
      tag = 0;
      do
        {
          /* fixme: check for overflow of out datatype */
          tag <<= 7;
          c = read_byte (d->reader);
          if (c == -1)
            return eof_or_error (d, 1);
          if (ti->nhdr >= DIM (ti->buf))
            return set_error(d, NULL, "tag+length header too large");
          ti->buf[ti->nhdr++] = c;
          tag |= c & 0x7f;
        }
      while (c & 0x80);
    }
  ti->tag = tag;

  /* Get the length */
  c = read_byte (d->reader);
  if (c == -1)
    return eof_or_error (d, 1);
  if (ti->nhdr >= DIM (ti->buf))
    return set_error(d, NULL, "tag+length header too large");
  ti->buf[ti->nhdr++] = c;
  if ( !(c & 0x80) )
    ti->length = c;
  else if (c == 0x80)
    {
      ti->ndef = 1;
      d->non_der = 1;
    }
  else if (c == 0xff)
      return set_error (d, NULL, "forbidden length value");
  else
    {
      unsigned long len = 0;
      int count = c & 0x7f;

      /* fixme: check for overflow of our length type */
      for (; count; count--)
        {
          len <<= 8;
          c = read_byte (d->reader);
          if (c == -1)
            return eof_or_error (d, 1);
          if (ti->nhdr >= DIM (ti->buf))
            return set_error(d, NULL, "tag+length header too large");
          ti->buf[ti->nhdr++] = c;
          len |= c & 0xff;
        }
      ti->length = len;
    }

  /* Without this kludge some example certs can't be parsed */
  if (ti->class == CLASS_UNIVERSAL && !ti->tag)
    ti->length = 0;

  return 0;
}


static int
cmp_tag (AsnNode node, const struct tag_info *ti)
{
  if (node->flags.class != ti->class)
    return 0;
  if (node->type == TYPE_TAG)
    {
      return_val_if_fail (node->valuetype == VALTYPE_ULONG, 0);
      return node->value.v_ulong == ti->tag;
    }
  if (node->type == ti->tag)
    return 1;
  if (ti->class == CLASS_UNIVERSAL)
    {
      if (node->type == TYPE_SEQUENCE_OF && ti->tag == TYPE_SEQUENCE)
        return 1;
      if (node->type == TYPE_SET_OF && ti->tag == TYPE_SET)
        return 1;
      if (node->type == TYPE_ANY && is_primitive_type (ti->tag))
        return 1; 
    }

  return 0;
}

/* Find the node in the tree ROOT corresponding to TI and return that
   node.  Returns NULL if the node was not found */
static AsnNode
find_anchor_node (AsnNode root, const struct tag_info *ti)
{
  AsnNode node = root;

  while (node)
    {
      if (cmp_tag (node, ti))
        {
          return node; /* found */
        }

      if (node->down)
        node = node->down;
      else if (node == root)
        return NULL; /* not found */
      else if (node->right)
        node = node->right;
      else 
        { /* go up and right */
          do
            {
              while (node->left && node->left->right == node)
                node = node->left;
              node = node->left;
              
              if (!node || node == root)
                return NULL; /* back at the root -> not found */
            }
          while (!node->right);
          node = node->right;
        }
    }

  return NULL;
}

static int
match_der (AsnNode root, const struct tag_info *ti,
           DECODER_STATE ds, AsnNode *retnode, int debug)
{
  AsnNode node;

  *retnode = NULL;
  node = ds->cur.node;
  if (!node)
    {
      if (debug)
        puts ("  looking for anchor");
      node = find_anchor_node (root,  ti);
      if (!node)
        fputs (" anchor node not found\n", stdout);
    }
  else if (ds->cur.again)
    {
      if (debug)
        puts ("  doing last again");
      ds->cur.again = 0;
    }
  else if (is_primitive_type (node->type) || node->type == TYPE_ANY
           || node->type == TYPE_SIZE || node->type == TYPE_DEFAULT )
    {
      if (debug)
        puts ("  primitive type - get next");
      if (node->right)
        node = node->right;
      else if (!node->flags.in_choice)
        node = NULL;
      else /* in choice */
        {
          if (debug)
            puts ("  going up after choice - get next");
          while (node->left && node->left->right == node)
            node = node->left;
          node = node->left; /* this is the up pointer */
          if (node)
            node = node->right;
        }
    }
  else if (node->type == TYPE_SEQUENCE_OF || node->type == TYPE_SET_OF)
    {
      if (debug)
        {
          printf ("  prepare for seq/set_of (%d %d)  ",
                  ds->cur.length, ds->cur.nread);
          printf ("  cur: ("); _ksba_asn_node_dump (node, stdout);
          printf (")\n");
          if (ds->cur.node->flags.in_array)
            puts ("  This is in an arrat!");
          if (ds->cur.went_up)
            puts ("  And we going up!");
        }
      if ((ds->cur.went_up && !ds->cur.node->flags.in_array) ||
          (ds->idx && ds->cur.nread >= ds->stack[ds->idx-1].length))
        {
          if (debug)
            printf ("  advancing\n");
          if (node->right)
            node = node->right;
          else
            {
              for (;;)
                {
                  while (node->left && node->left->right == node)
                    node = node->left;
                  node = node->left; /* this is the up pointer */
                  if (!node)
                    break;
                  if (node->right)
                    {
                      node = node->right;
                      break;
                    }
                }
            }
        }
      else if (ds->cur.node->flags.in_array
               && ds->cur.went_up)
        {
          if (debug)
            puts ("  Reiterating");
        }
      else
        node = node->down;
    }
  else /* constructed */
    {
      if (debug)
        {
          printf ("  prepare for constructed (%d %d) ",
                  ds->cur.length, ds->cur.nread);
          printf ("  cur: ("); _ksba_asn_node_dump (node, stdout);
          printf (")\n");
          if (ds->cur.node->flags.in_array)
            puts ("  This is in an arrat!");
          if (ds->cur.went_up)
            puts ("  And we going up!");
        }
      ds->cur.in_seq_of = 0;

      if (ds->cur.node->flags.in_array
          && ds->cur.went_up)
        {
          if (debug)
            puts ("  Reiterating this");
        }
      else if (ds->cur.went_up || ds->cur.next_tag)
        {
          if (node->right)
            node = node->right;
          else
            {
              for (;;)
                {
                  while (node->left && node->left->right == node)
                    node = node->left;
                  node = node->left; /* this is the up pointer */
                  if (!node)
                    break;
                  if (node->right)
                    {
                      node = node->right;
                      break;
                    }
                }
            }
        }
      else 
        node = node->down;
      
    }
  if (!node)
    return -1;
  ds->cur.node = node;
  ds->cur.went_up = 0;
  ds->cur.next_tag = 0;

  if (debug)
    {
      printf ("  Expect ("); _ksba_asn_node_dump (node, stdout); printf (")\n");
    }

  if (node->flags.skip_this)
    return 1;

  if (node->type == TYPE_SIZE)
    {
      if (debug)
        printf ("   skipping size tag\n");
      return 1;
    }
  if (node->type == TYPE_DEFAULT)
    {
      if (debug)
        printf ("   skipping default tag\n");
      return 1;
    }

  if ( cmp_tag (node, ti))
    {
      *retnode = node;
      return 3;
    }
    
  if (node->type == TYPE_CHOICE)
    {
      if (debug)
        printf ("   testing choice...\n");
      for (node = node->down; node; node = node->right)
        {
          if (debug)
            {
              printf ("       %s (", node->flags.skip_this? "skip":" cmp");
              _ksba_asn_node_dump (node, stdout);
              printf (")\n");
            }

          if (!node->flags.skip_this && cmp_tag (node, ti))
            {
              if (debug)
                {
                  printf ("  choice match <"); dump_tlv (ti, stdout);
                  printf (">\n");
                }
              /* mark the remaining as done */
              for (node=node->right; node; node = node->right)
                node->flags.skip_this = 1;
              return 1;
            }
          node->flags.skip_this = 1;
        }
      node = ds->cur.node; /* reset */
    }

  if (node->flags.in_choice)
    {
      if (debug)
        printf ("   skipping non matching choice\n");
      return 1;
    }
  
  if (node->flags.is_optional)
    {
      if (debug)
        printf ("   skipping optional element\n");
      if (node->type == TYPE_TAG)
        ds->cur.next_tag = 1;
      return 1;
    }

  if (node->flags.has_default)
    {
      if (debug)
        printf ("   use default value\n");
      if (node->type == TYPE_TAG)
        ds->cur.next_tag = 1;
      *retnode = node;
      return 2;
    }

  return -1;
}


static KsbaError 
decoder_init (BerDecoder d)
{
  d->ds = new_decoder_state ();

  d->root = _ksba_asn_expand_tree (d->module);
  clear_help_flags (d->root);
  d->bypass = 0;

  return 0;
}

static void
decoder_deinit (BerDecoder d)
{
  release_decoder_state (d->ds);
  d->ds = NULL;
  d->val.node = NULL;
}


static KsbaError
decoder_next (BerDecoder d)
{
  struct tag_info ti;
  AsnNode node;
  KsbaError err;
  DECODER_STATE ds = d->ds;
  int debug = d->debug;

  err = read_tl (d, &ti);
  if (err)
    {
      return err;
    }

  if (debug)
    {
      printf ("ReadTLV <"); dump_tlv (&ti, stdout); printf (">\n");
    }

  if (d->use_image)
    {
      if (!d->image.buf)
        {
          d->image.length = ti.length + 100;
          d->image.used = 0;
          d->image.buf = xtrymalloc (d->image.length);
          if (!d->image.buf)
            return KSBA_Out_Of_Core;
        }

      if (ti.nhdr + d->image.used >= d->image.length)
        return set_error (d, NULL, "image buffer too short to store the tag");
      memcpy (d->image.buf + d->image.used, ti.buf, ti.nhdr);
      d->image.used += ti.nhdr;
    }
  

  if (!d->bypass)
    {
      int again;

      do
        {
          again = 0;
          switch ( match_der (d->root, &ti, ds, &node, debug) )
            { 
            case -1:
              if (debug)
                {
                  printf ("   FAIL <"); dump_tlv (&ti, stdout); printf (">\n");
                }
              d->bypass = 1;
              break;
            case 0:
              if (debug)
                puts ("  End of description");
              d->bypass = 1;
              break;
            case 1: /* again */
              if (debug)
                printf ("  Again\n");
              again = 1;
              break;
            case 2: /* use default value +  again*/
              if (debug)
                printf ("  Using default\n");
              again = 1;
              break;
            case 3: /* match */ 
              if (debug)
                {
                  printf ("  Match <"); dump_tlv (&ti, stdout); printf (">\n");
                }
              /* increment by the header length */
              ds->cur.nread += ti.nhdr;
              /*inc_decoder_state (ds, ti.nhdr);*/
                  
              if (!ti.is_constructed)
                { 
                  ds->cur.nread += ti.length;
                  /*inc_decoder_state (ds, ti.length);*/
                }

              ds->cur.went_up = 0;
              do
                {
                  if (debug)
                    printf ("  (length %d nread %d) %s\n",
                            ds->idx? ds->stack[ds->idx-1].length:-1,
                            ds->cur.nread,
                            ti.is_constructed? "con":"pri");

                  if ( ds->idx
                       && !ds->stack[ds->idx-1].ndef_length
                       && (ds->cur.nread
                           > ds->stack[ds->idx-1].length)) 
                    {
                      fprintf (stderr, "  ERROR: object length field %d octects"
                               " too large\n",   
                              ds->cur.nread > ds->cur.length);
                      ds->cur.nread = ds->cur.length;
                    }
                  if ( ds->idx
                       && !ds->stack[ds->idx-1].ndef_length
                       && (ds->cur.nread
                           >= ds->stack[ds->idx-1].length)) 
                    {
                      int n = ds->cur.nread;
                      pop_decoder_state (ds);
                      ds->cur.nread += n;
                      ds->cur.went_up++;
                    }
                }
              while ( ds->idx
                      && !ds->stack[ds->idx-1].ndef_length
                      && (ds->cur.nread
                          >= ds->stack[ds->idx-1].length));
                  
              if (ti.is_constructed)
                {
                  /* prepare for the next level */
                  ds->cur.length = ti.length;
                  ds->cur.ndef_length = ti.ndef;
                  push_decoder_state (ds);
                  ds->cur.length = 0;
                  ds->cur.ndef_length = 0;
                  ds->cur.nread = 0;
                }
              if (debug)
                printf ("  (length %d nread %d) end\n",
                        ds->idx? ds->stack[ds->idx-1].length:-1,
                        ds->cur.nread);
              break;
            default:
              never_reached ();
              abort (); 
              break;
            }
        }
      while (again);
    }

  d->val.primitive = !ti.is_constructed;
  d->val.length = ti.length;
  d->val.nhdr = ti.nhdr;
  d->val.node = d->bypass? NULL : node;
  if (debug)
    dump_decoder_state (ds);
  
  return 0;
}

static KsbaError
decoder_skip (BerDecoder d)
{
  if (d->val.primitive)
    { 
      int n;
      
      for (n=0; n < d->val.length; n++)
        if (read_byte (d->reader) == -1)
          return eof_or_error (d, 1);
    }
  return 0;
}



/* Calculate the distance between the 2 nodes */
static int
distance (AsnNode root, AsnNode node)
{
  int n=0;

  while (node && node != root)
    {
      while (node->left && node->left->right == node)
        node = node->left;
      node = node->left;
      n++;
    }

  return n;
}


/**
 * _ksba_ber_decoder_dump:
 * @d: Decoder object
 * 
 * Dump a textual representation of the encoding to the given stream.
 * 
 * Return value: 
 **/
KsbaError
_ksba_ber_decoder_dump (BerDecoder d, FILE *fp)
{
  KsbaError err;
  int depth = 0;
  AsnNode node;
  unsigned char *buf = NULL;
  size_t buflen = 0;;

  if (!d)
    return KSBA_Invalid_Value;

  d->debug = !!getenv("DEBUG_BER_DECODER");
  d->use_image = 0;
  d->image.buf = NULL;
  err = decoder_init (d);
  if (err)
    return err;

  while (!(err = decoder_next (d)))
    {
      node = d->val.node;
      if (node)
        depth = distance (d->root, node);

      fprintf (fp, "%4lu %4u:%*s",
               ksba_reader_tell (d->reader) - d->val.nhdr,
               d->val.length,
               depth*2, "");
      if (node)
        _ksba_asn_node_dump (node, fp);
      else
        fputs ("[No matching node]", fp);

      if (node && d->val.primitive)
        {
          int i, n, c;
          char *p;
      
          if (!buf || buflen < d->val.length)
            {
              xfree (buf);
              buflen = d->val.length + 100;
              buf = xtrymalloc (buflen);
              if (!buf)
                err = KSBA_Out_Of_Core;
            }

          for (n=0; !err && n < d->val.length; n++)
            {
              if ( (c=read_byte (d->reader)) == -1)
                err =  eof_or_error (d, 1);
              buf[n] = c;
            }
          if (err)
            break;
          fputs ("  (", fp);
          p = NULL;
          switch (node->type)
            {
            case TYPE_OBJECT_ID:
              p = ksba_oid_to_str (buf, n);
              break;
            default:
              for (i=0; i < n && i < 20; i++)
                fprintf (fp,"%02x", buf[i]);
              if (i < n)
                fputs ("..more..", fp);
              break;
            }
          if (p)
            {
              fputs (p, fp);
              xfree (p);
            }
          fputs (")\n", fp);
        }
      else
        {
          err = decoder_skip (d);
          putc ('\n', fp);
        }
      if (err)
        break;

    }
  if (err == -1)
    err = 0;

  decoder_deinit (d);
  xfree (buf);
  return err;
}




KsbaError
_ksba_ber_decoder_decode (BerDecoder d, AsnNode *r_root,
                          unsigned char **r_image, size_t *r_imagelen)
{
  KsbaError err;
  AsnNode node;
  unsigned char *buf = NULL;
  size_t buflen = 0;;

  if (!d)
    return KSBA_Invalid_Value;

  if (r_root)
    *r_root = NULL;

  d->use_image = 1;
  d->image.buf = NULL;

  err = decoder_init (d);
  if (err)
    return err;

  while (!(err = decoder_next (d)))
    {
      int n, c;

      node = d->val.node;
      if (node && d->use_image)
        {
          node->off = ksba_reader_tell (d->reader) - d->val.nhdr;
          node->nhdr = d->val.nhdr;
          node->len = d->val.length;
          if (d->image.used + d->val.length > d->image.length)
            err = set_error(d, NULL, "TLV length too large");
          else if (d->val.primitive)
            {
              for (n=0; n < d->val.length; n++)
                {
                  if ( (c=read_byte (d->reader)) == -1)
                    {
                      err = eof_or_error (d, 1);
                      break;
                    }
                  d->image.buf[d->image.used++] = c;
                }
            }
        }
      else if (node && d->val.primitive)
        {
          if (!buf || buflen < d->val.length)
            {
              xfree (buf);
              buflen = d->val.length + 100;
              buf = xtrymalloc (buflen);
              if (!buf)
                err = KSBA_Out_Of_Core;
            }

          for (n=0; !err && n < d->val.length; n++)
            {
              if ( (c=read_byte (d->reader)) == -1)
                err =  eof_or_error (d, 1);
              buf[n] = c;
            }
          if (err)
            break;

          switch (node->type)
            {
            default:
              _ksba_asn_set_value (node, VALTYPE_MEM, buf, n);
              break;
            }
        }
      else
        {
          err = decoder_skip (d);
        }
      if (err)
        break;
    }
  if (err == -1)
    err = 0;

  if (r_root && !err)
    {
      *r_root = d->root;
      d->root = NULL;
      *r_image = d->image.buf;
      d->image.buf = NULL;
      *r_imagelen = d->image.used;
    }

  decoder_deinit (d);
  xfree (buf);
  return err;
}

