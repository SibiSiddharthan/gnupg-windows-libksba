/* writer.c - provides the Writer object
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
#include <errno.h>
#include "util.h"

#include "ksba.h"
#include "writer.h"

/**
 * ksba_writer_new:
 * 
 * Create a new but uninitialized KsbaWriter Object.  Using this
 * write object in unitialized state does always return an errorf.
 * 
 * Return value: KsbaWriter Object or NULL in case of memory shortage.
 **/
KsbaWriter
ksba_writer_new (void)
{
  KsbaWriter w;

  w = xtrycalloc (1, sizeof *w);
  if (!w)
    return NULL;

  return w;
}


/**
 * ksba_writer_release:
 * @w: Writer Object (or NULL)
 * 
 * Release this object
 **/
void
ksba_writer_release (KsbaWriter w)
{
  xfree (w);
}

int
ksba_writer_error (KsbaWriter w)
{
  return w? w->error : -1;
}

unsigned long
ksba_writer_tell (KsbaWriter w)
{
  return w? w->nwritten : 0;
}


/**
 * ksba_writer_set_fd:
 * @w: Writer object
 * @fd: file descriptor
 * 
 * Initialize the Writer object with a file descriptor, so that write
 * operations on this object are excuted on this file descriptor.
 * 
 * Return value: 
 **/
KsbaError
ksba_writer_set_fd (KsbaWriter w, int fd)
{
  if (!w || fd == -1)
    return KSBA_Invalid_Value;
  if (w->type)
    return KSBA_Conflict;

  w->error = 0;
  w->type = WRITER_TYPE_FD;
  w->u.fd = fd;

  return 0;
}

/**
 * ksba_writer_set_file:
 * @w: Writer object
 * @fp: file pointer
 * 
 * Initialize the Writer object with a stdio file pointer, so that write
 * operations on this object are excuted on this stream
 * 
 * Return value: 
 **/
KsbaError
ksba_writer_set_file (KsbaWriter w, FILE *fp)
{
  if (!w || !fp)
    return KSBA_Invalid_Value;
  if (w->type)
    return KSBA_Conflict;

  w->error = 0;
  w->type = WRITER_TYPE_FILE;
  w->u.file = fp;
  return 0;
}



/**
 * ksba_writer_set_cb:
 * @w: Writer object
 * @cb: Callback function
 * @cb_value: Value passed to the callback function
 * 
 * Initialize the writer object with a callback function.
 * This callback function is defined as:
 * <literal>
 * typedef int (*cb) (void *cb_value, 
 *                    const void *buffer, size_t count);
 * </literal>
 *
 * The callback is expected to process all @count bytes from @buffer
 * @count should not be 0 and @buffer should not be %NULL
 * The callback should return 0 on success or an %KSBA_xxx error code.
 * 
 * Return value: 0 on success or an error code
 **/
KsbaError
ksba_writer_set_cb (KsbaWriter w, 
                    int (*cb)(void*,const void *,size_t), void *cb_value )
{
  if (!w || !cb)
    return KSBA_Invalid_Value;
  if (w->type)
    return KSBA_Conflict;
  
  w->error = 0;
  w->type = WRITER_TYPE_CB;
  w->u.cb.fnc = cb;
  w->u.cb.value = cb_value;

  return 0;
}


/**
 * ksba_writer_write:
 * @w: Writer object
 * @buffer: A buffer with the data to be written
 * @length: The length of this buffer
 * 
 * Write @length bytes from @buffer.
 *

 * Return value: 0 on success or an error code
 **/
KsbaError
ksba_writer_write (KsbaWriter w, const void *buffer, size_t length)
{
  if (!w)
    return KSBA_Invalid_Value;

  if (!buffer)
      return KSBA_Not_Implemented;

  if (!w->type)
    {
      w->error = -1;
      return KSBA_General_Error;
    }
  else if (w->type == WRITER_TYPE_FILE)
    {
      if (!length)
        return 0;

      if ( fwrite (buffer, length, 1, w->u.file) == 1)
        {
          w->nwritten += length;
        }
      else
        {
          w->error = errno;
          return KSBA_Write_Error;
        }
    }
  else if (w->type == WRITER_TYPE_CB)
    {
      int err = w->u.cb.fnc (w->u.cb.value, buffer, length);
      if (err)
        return err;
      w->nwritten += length;
    }
  else 
    return KSBA_Bug;
  
  return 0;
} 




