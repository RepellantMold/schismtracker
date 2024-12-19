/*
 * Schism Tracker - a cross-platform Impulse Tracker clone
 * copyright (c) 2003-2005 Storlek <storlek@rigelseven.com>
 * copyright (c) 2005-2008 Mrs. Brisby <mrs.brisby@nimh.org>
 * copyright (c) 2009 Storlek & Mrs. Brisby
 * copyright (c) 2010-2012 Storlek
 * URL: http://schismtracker.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "headers.h"
#include "bswap.h"
#include "fmt.h"
#include "it.h"
#include "song.h"

#include "player/sndfile.h"

#include "log.h"

// RM: all my research comes from
// * https://moddingwiki.shikadi.net/wiki/VOC_Format
// * https://wiki.multimedia.cx/index.php?title=Creative_Voice

enum VOC_BLOCK_TYPES {
	VOC_BLOCK_TERMINATOR = 0, // this seems to be optional?
	VOC_BLOCK_SOUND = 1,
	VOC_BLOCK_SOUND_WITHOUT_TYPE = 2,
	VOC_BLOCK_SILENCE = 3,
	VOC_BLOCK_MARKER = 4,
	VOC_BLOCK_TEXT = 5,
	VOC_BLOCK_REPEAT_START = 6,
	VOC_BLOCK_REPEAT_END = 7,
	VOC_BLOCK_SOUND_EXTRA = 8,
	VOC_BLOCK_SOUND_NEW = 9
};

enum VOC_CODEC {
	VOC_8BIT_UNSIGNED_PCM = 0,
	VOC_4BIT_CREATIVE_ADPCM = 1, // all of these scale up to 8-bit PCM
	VOC_3BIT_CREATIVE_ADPCM = 2, // this one is called "2.6 bits" as well ????
	VOC_2BIT_CREATIVE_ADPCM = 3,
	VOC_16BIT_SIGNED_PCM = 4,
	VOC_ALAW = 6,
	VOC_ULAW = 7,
	VOC_4BIT_CREATIVE_ADPCM_ALT = 0x200 // this scales to 16-bit (probably won't support)
};

struct voc_block {
	struct {
		enum VOC_BLOCK_TYPES type;
		uint32_t length; // for some reason this is 3 bytes... -_-
	} header;
	struct {
		uint8_t frequency_divisor;
		uint32_t sample_rate;
		uint8_t channel_count;
		uint8_t bit_depth;
		enum VOC_CODEC codec;
	} audio; // trying to cover block types 0, 8, and 9
	uint8_t* data;
};

/* --------------------------------------------------------------------------------------------------------- */

static int voc_block_fmt_read(struct voc_block *block, slurp_t *fp)
{
	uint8_t b[3];

	if ((block->header.type = slurp_getc(fp)) == EOF)
		return 0;
	if (slurp_read(fp, b, 3) != 3)
		return 0;
	block->header.length = (b[2] << 16) | (b[1] << 8) | (b[0]);
	//log_appendf(3, "size:%d block type:%d", block->header.length, block->header.type);
	block->data = mem_calloc(block->header.length, 1);
	if (slurp_eof(fp) || 
		slurp_read(fp, block->data, block->header.length) != block->header.length)
		return 0;

	return 1;
}

/* --------------------------------------------------------------------------------------------------------- */

static int voc_load(slurp_t *fp, int slot, int load_sample)
{
	char sig[20];
	uint16_t version;
	uint16_t checksum;
	struct voc_block block = {0};
	struct instrumentloader ii;
	song_instrument_t *g = instrument_loader_init(&ii, slot);
	int audio_occurences = 0;

	slurp_read(fp, sig, 20);
	if (memcmp(sig, "Creative Voice File\x1A", 20) != 0)
		return 0;

	slurp_seek(fp, 2, SEEK_CUR); // size of main header
	slurp_read(fp, &version, 2);
	version = bswapLE16(version);
	//if (version != 0x010A || version != 0x114)
		//return 0;
	slurp_read(fp, &checksum, 2);
	checksum = bswapLE16(checksum);
	if (checksum != (~version + 0x1234))
		return 0;

	// XXX what should we do if multiple sound blocks appear?
	// discard them? concatenate them?
	while(voc_block_fmt_read(&block, fp)) {
		uint32_t flags = SF_LE;
		slurp_t fp2;
		slurp_memstream(&fp2, (uint8_t*)block.data, block.header.length);
		song_sample_t *smp = song_get_sample(audio_occurences);
		smp->flags = 0;
		smp->length = block.header.length;

		switch(block.header.type) {
#if 0
			// TODO: check if i'm correct on this...
			case VOC_BLOCK_REPEAT_START:
				uint8_t tmp[2];
				slurp_read(&fp2, tmp, 2);
				if (tmp[0] != 0xFF && tmp[1] != 0xFF)
					log_appendf(4, " Warning: limited loop counts are not supported!");
				smp->loop_start = slurp_tell(fp);
				break;
			case VOC_BLOCK_REPEAT_END:
				smp->loop_end = slurp_tell(fp);
				if (smp->loop_end < smp->loop_start)
					smp->loop_end = smp->loop_start = 0; // that was crap
				else
					smp->flags |= CHN_LOOP;
				break;
#endif

			case VOC_BLOCK_SOUND_NEW:
				uint16_t tmp_codec;
				uint32_t tmp_sample_rate;
				audio_occurences++;
				slurp_read(&fp2, &tmp_sample_rate, sizeof(uint32_t));
				block.audio.sample_rate = bswapLE32(tmp_sample_rate);
				block.audio.bit_depth = slurp_getc(&fp2);
				block.audio.channel_count = slurp_getc(&fp2);
				smp->c5speed = block.audio.sample_rate;
				smp->length -= 12;
				slurp_read(&fp2, &tmp_codec, sizeof(uint16_t));
				tmp_codec = bswapLE16(tmp_codec);
				block.audio.codec = tmp_codec;
				switch (block.audio.codec) {
				case VOC_8BIT_UNSIGNED_PCM:
					flags |= SF_PCMU;
					break;
				case VOC_16BIT_SIGNED_PCM:
					flags |= SF_PCMS;
					break;
				default: // TODO: alaw and mulaw/ADPCM decoding
					return 0;
					break;
				}
				// why the hell does this exist when the codec type byte exists?!?!
				if (block.audio.bit_depth == 8)
					flags |= SF_8;
				else if (block.audio.bit_depth == 16)
					flags |= SF_16;
				slurp_seek(&fp2, 4, SEEK_CUR); // reserved
				goto read_sample;
				break;

			case VOC_BLOCK_SOUND_EXTRA:
			case VOC_BLOCK_SOUND:
				uint8_t tmp_divisor[2];
				audio_occurences++;
				tmp_divisor[0] = slurp_getc(&fp2);
				if (block.header.type == VOC_BLOCK_SOUND_EXTRA) {
					tmp_divisor[1] = slurp_getc(&fp2);
					block.audio.frequency_divisor = (tmp_divisor[0] << 8) + tmp_divisor[1];
					smp->length -= 4;
				} else {
					block.audio.frequency_divisor = tmp_divisor[0];
					smp->c5speed = 1000000 / (256 - block.audio.frequency_divisor);
					smp->length -= 2;
				}
				block.audio.codec = slurp_getc(&fp2);
				switch (block.audio.codec) {
				case VOC_8BIT_UNSIGNED_PCM:
					flags |= SF_PCMU | SF_8;
					break;
				case VOC_16BIT_SIGNED_PCM:
					flags |= SF_PCMS | SF_16;
					break;
				default: // TODO: alaw and mulaw/ADPCM decoding
					return 0;
					break;
				}
				if (block.header.type == VOC_BLOCK_SOUND_EXTRA) {
					block.audio.channel_count = slurp_getc(&fp2);
					flags |= block.audio.channel_count ? SF_SI : SF_M;
					smp->c5speed =  256000000 / ((block.audio.channel_count + 1) * (65536 - block.audio.frequency_divisor));
				} else {
					flags |= SF_M;
				}
				goto read_sample;
				break;
			
			case VOC_BLOCK_TEXT:
				strncpy(smp->name, block.data, MAX(21, slurp_length(&fp2)));
				break;
			case VOC_BLOCK_MARKER:
			case VOC_BLOCK_SILENCE:
				continue;
			case VOC_BLOCK_TERMINATOR: default: break;
			case VOC_BLOCK_SOUND_WITHOUT_TYPE:
			read_sample:
				instrument_loader_sample(&ii, audio_occurences + 1);
				break;
		}

		unslurp(&fp2);
		free(block.data);
	}

	return 1;
}

/* --------------------------------------------------------------------------------------------------------- */

int fmt_voc_load_instrument(slurp_t *fp, int slot)
{
	if (!slot)
		return 0;

	return voc_load(fp, slot, 1);
}

int fmt_voc_read_info(dmoz_file_t *file, slurp_t *fp)
{
	if (!voc_load(fp, 1, 0))
		return 0;

	file->description  = "Creative VOC";
	file->type         = TYPE_INST_XI;
	return 1;
}
