/* ber-decoder.h - Definitions for the Basic Encoding Rules Decoder
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

#ifndef BER_DECODER_H
#define BER_DECODER_H 1

#include "asn1-func.h"

struct ber_decoder_s;
typedef struct ber_decoder_s *BerDecoder;

BerDecoder _ksba_ber_decoder_new (void);
void       _ksba_ber_decoder_release (BerDecoder d);

KsbaError _ksba_ber_decoder_set_module (BerDecoder d, KsbaAsnTree module);
KsbaError _ksba_ber_decoder_set_reader (BerDecoder d, KsbaReader r);

KsbaError _ksba_ber_decoder_dump (BerDecoder d, FILE *fp);
KsbaError _ksba_ber_decoder_decode (BerDecoder d, AsnNode *r_root,
                                    unsigned char **r_image,
                                    size_t *r_imagelen);



#endif /*BER_DECODER_H*/