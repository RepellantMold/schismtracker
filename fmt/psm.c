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
#include "charset.h"
#include "slurp.h"
#include "fmt.h"
#include "log.h"

#include "player/sndfile.h"

#include <inttypes.h> /* need uint*_t format specifiers */

/* --------------------------------------------------------------------- */

/* pattern byte flags/masks */
enum {
	PSM_PAT_NOTE_PRESENT = 0x80,
	PSM_PAT_INST_PRESENT = 0x40,
	PSM_PAT_VOL_PRESENT = 0x20,
	PSM_PAT_CMD_PRESENT = 0x10,
	PSM_PAT_CHN_NUM_MASK = 0x0F,
};

enum {
	PSM_OCHK_END = 0,
	PSM_OCHK_LIST = 1,
	PSM_OCHK_PLAYRANGE = 2,
	PSM_OCHK_JUMPLOOP = 3,
	PSM_OCHK_JUMPLINE = 4,
	PSM_OCHK_CHNFLIP = 5,
	PSM_OCHK_TRANSPOSE = 6,
	PSM_OCHK_SETSPEED = 7,
	PSM_OCHK_SETTEMPO = 8,
	PSM_OCHK_SAMPLEMAP = 0xC,
	PSM_OCHK_CHANNELPAN = 0xD,
	PSM_OCHK_CHANNELVOL = 0xE,
};

#pragma pack(push, 1)

typedef struct {
	uint8_t opcode;
	uint8_t data[];
} psm_order_chunk_t;

typedef struct {
	uint16_t rowsize;
	uint8_t data[];
} psm_pattern_channel_t;

typedef union chunkdata {
	struct {
		uint8_t songtype[9];
		uint8_t compression;
		uint8_t chnnum;
	} SONG;

	struct {
		uint8_t text[6];
	} DATE;

	struct {
		uint8_t flags;
		uint8_t songname[8];
		uint8_t insx[4];
		uint8_t name[33];
		uint8_t x[6];
		uint16_t id;
		uint32_t length;
		uint32_t loop_start;
		uint32_t loop_end;
		uint16_t unknown1;
		uint8_t volume;
		uint32_t unknown2;
		uint32_t c5speed;
		uint8_t unknown3[19];
		uint8_t smp_bytes[];
	} DSMP;

	struct {
		uint32_t length;
		uint8_t px[4];
		uint16_t rownum;
		psm_pattern_channel_t data[];
	} PBOD;

	struct {
		uint16_t chunk_count;
		psm_order_chunk_t chunks[];
	} OPLH;

	struct {
		uint32_t list_size;
		uint8_t list[];
	} PATT;
} chunkdata_t;

typedef struct chunk {
	uint32_t id;
	uint32_t size;
	const chunkdata_t *data;
} chunk_t;

#pragma pack(pop)

#define ID_PSM2 0x50534D20
#define ID_TITL 0x5449544C
#define ID_FILE 0x46494C45
#define ID_SDFT 0x53444654
#define ID_PBOD 0x50424F44
#define ID_SONG 0x534F4E47
#define ID_DATE 0x44415445
#define ID_OPLH 0x4F504C48
#define ID_PATT 0x50415454
#define ID_DSAM 0x4453414D
#define ID_DSMP 0x44534D50

// 'chunk' is filled in with the chunk header
// return: 0 if chunk overflows EOF, 1 if it was successfully read
// pos is updated to point to the beginning of the next chunk
static int _chunk_read(chunk_t *chunk, const uint8_t *data, size_t length, size_t *pos)
{
	if (*pos + 8 > length)
		return 0;
	memcpy(&chunk->id, data + *pos, 4);
	memcpy(&chunk->size, data + *pos + 4, 4);
	chunk->id = bswapBE32(chunk->id);
	chunk->size = bswapLE32(chunk->size); /* yes, size is stored as little endian */
	chunk->data = (chunkdata_t *) (data + *pos + 8);
	*pos += 8 + chunk->size;
	return (*pos <= length);
}


int fmt_psm_read_info(dmoz_file_t *file, const uint8_t *data, size_t length)
{
	uint8_t psm[4], filee[4];

	if (!(length > 40))
		return 0;

	memcpy(psm, data, 4);
	memcpy(filee, data + 8, 4);

	if (memcmp(psm, "PSM ", 4) || memcmp(filee, "FILE", 4))
		return 0;

	file->description = "Protracker Studio";
	/*file->extension = str_dup("psm");*/
	file->type = TYPE_MODULE_MOD;
	//file->title = strn_dup((const char *)&data[20], 20);
	return 1;
}

int fmt_psm_load_song(song_t *song, slurp_t *fp, unsigned int lflags)
{
	chunk_t chunk, subchunk;
	uint8_t psm[4], filee[4];
	size_t pos = 0, pos2 = 0, length = 0;
	size_t s = 0, p = 0, n = 0;
	uint16_t nord = 0, nsmp = 0, npat = 0, nchn = 0;
	uint8_t chn_doesnt_match = 0, sinaria = 0;
	size_t num_song_headers = 0;

	slurp_read(fp, &psm, 4);
	slurp_seek(fp, 4, SEEK_CUR);
	slurp_read(fp, &filee, 4);

	if (memcmp(psm, "PSM ", 4) || memcmp(filee, "FILE", 4))
		return LOAD_UNSUPPORTED;

	pos = slurp_tell(fp);
	slurp_seek(fp, 0, SEEK_END);
	length = slurp_tell(fp);
	slurp_rewind(fp);

			/* make sure our offset doesn't pass the length */
#define PSM_ASSERT_OFFSET(o, l) \
	if ((o) >= (l)) { \
		log_appendf(4, " WARNING: Offset (%"PRIu16") passed length (%"PRIu16") while parsing pattern!", (uint16_t)(o), (uint16_t)(l)); \
		break; \
	}

	while (_chunk_read(&chunk, fp->data, length, &pos)) {
		switch(chunk.id) {
		case ID_TITL:
			strncpy(song->title, (char*)&chunk.data, MIN(bswapLE32(chunk.size), 25));
			song->title[25] = '\0';
			break;
		case ID_SONG:
			nchn = chunk.data->SONG.chnnum;

			while (_chunk_read(&subchunk, fp->data, chunk.size, &pos2)) {
				case ID_DATE:
					if (memcmp(subchunk.data->DATE.text, "800211", 8) == 0
						|| memcmp(subchunk.data->DATE.text, "940902", 8) == 0
						|| memcmp(subchunk.data->DATE.text, "940903", 8) == 0
						|| memcmp(subchunk.data->DATE.text, "940906", 8) == 0
						|| memcmp(subchunk.data->DATE.text, "940914", 8) == 0
						|| memcmp(subchunk.data->DATE.text, "941213", 8) == 0)
						sinaria = 1;
					break;
				case ID_OPLH:
					if (subchunk.size < 9)
						break;

					uint16_t offset = 0, length = subchunk.size;

					while (offset < length) {
						uint8_t opcode = subchunk.data->OPLH.chunks[offset++].opcode;

						PSM_ASSERT_OFFSET(offset, length)

						switch(opcode) {
							case PSM_OCHK_END:
								break;

							case PSM_OCHK_LIST:
								uint8_t id[4];

								memcpy(id, subchunk.data->OPLH.chunks->data + offset, 4);
								offset += 4;
								npat = atoi((char*)&id[1]);
								break;

							case PSM_OCHK_PLAYRANGE:
								subchunk.data->OPLH.chunks->data[offset += 4];
								break;

							case PSM_OCHK_JUMPLOOP:
							case PSM_OCHK_JUMPLINE:
								subchunk.data->OPLH.chunks->data[offset++];
								break;

							case PSM_OCHK_CHNFLIP:
								n = subchunk.data->OPLH.chunks->data[offset++];
								subchunk.data->OPLH.chunks->data[offset++];
								break;

							case PSM_OCHK_TRANSPOSE:
								subchunk.data->OPLH.chunks->data[offset++];
								break;
								
							case PSM_OCHK_SETSPEED:
								song->initial_speed = subchunk.data->OPLH.chunks->data[offset++];
								break;

							case PSM_OCHK_SETTEMPO:
								song->initial_tempo = subchunk.data->OPLH.chunks->data[offset++];
								break;

							case PSM_OCHK_SAMPLEMAP:
								subchunk.data->OPLH.chunks->data[offset += 6];
								break;

							case PSM_OCHK_CHANNELPAN:
								n = subchunk.data->OPLH.chunks->data[offset++];
								uint8_t pan = subchunk.data->OPLH.chunks->data[offset++];
								uint8_t type = subchunk.data->OPLH.chunks->data[offset++];
								switch(type) {
									case 0:
										song->channels[n].panning = pan ^ 128;
										break;

									case 2:
										song->channels[n].panning = 128;
										song->channels[n].flags |= CHN_SURROUND;
										break;

									case 4:
										song->channels[n].panning = 128;
										break;
								}
								
								break;
								
							case PSM_OCHK_CHANNELVOL:
								n = subchunk.data->OPLH.chunks->data[offset++];
								song->channels[n].volume = (subchunk.data->OPLH.chunks->data[offset++] / 4) + 1;
								break;

							default:
								return LOAD_FORMAT_ERROR;
								break;
							}
					}
				break;
			}
			break;
		case ID_PBOD:
			if (p > MAX_PATTERNS)
				return LOAD_UNSUPPORTED; /* punt */

			if (lflags & LOAD_NOPATTERNS) {
				p++;
				continue;
			}

			p = atoi(&chunk.data->PBOD.px[1]) + atoi(&chunk.data->PBOD.px[2]);

			uint16_t offset = 0, length = bswapLE32(chunk.data->PBOD.length);
			
			while (p < npat) {
				uint16_t rowsize = bswapLE16(chunk.data->PBOD.data->rowsize);

				if (rowsize <= 2)
					break;

				song->patterns[p] = csf_allocate_pattern(rowsize - 2);

				int row = 0;

				while (row++ < rowsize - 2) {
			
					uint8_t mask = chunk.data->PBOD.data->data[offset++];

					PSM_ASSERT_OFFSET(offset, chunk.data->PBOD.length)

					uint8_t chn = chunk.data->PBOD.data->data[offset++];

					PSM_ASSERT_OFFSET(offset, chunk.data->PBOD.length)

					//if (chn > MAX_CHANNELS) /* whoops */
						//return LOAD_UNSUPPORTED;

					if (chn > nchn) /* header doesn't match? warn. */
						chn_doesnt_match = MAX(chn, chn_doesnt_match);

					song_note_t *note = song->patterns[p] + 64 * (row - 1) + chn;
					if (mask & PSM_PAT_NOTE_PRESENT) {
						uint8_t c = chunk.data->PBOD.data->data[offset++];

						PSM_ASSERT_OFFSET(offset, chunk.data->PBOD.length)

						if (c <= 168)
							note->note = c + 12;
					}

					if (mask & PSM_PAT_INST_PRESENT) {
						note->instrument = chunk.data->PBOD.data->data[offset++];

						PSM_ASSERT_OFFSET(offset, chunk.data->PBOD.length)
					}

					if (mask & PSM_PAT_VOL_PRESENT) {
						/* volume */
						uint8_t param = chunk.data->PBOD.data->data[offset++];

						PSM_ASSERT_OFFSET(offset, chunk.data->PBOD.length)
						note->voleffect = VOLFX_VOLUME;
						note->volparam = (param + 1) >> 1;
					}

					if (mask & PSM_PAT_CMD_PRESENT) {
						note->effect = chunk.data->PBOD.data->data[offset++];

						PSM_ASSERT_OFFSET(offset, chunk.data->PBOD.length)

						note->param = chunk.data->PBOD.data->data[offset++];

						PSM_ASSERT_OFFSET(offset, chunk.data->PBOD.length)

						//csf_import_mod_effect(note, 0);
					}
				}

			}

#undef PSM_ASSERT_OFFSET
			break;
		case ID_DSMP:
			/* sanity check. it doesn't matter if nsmp isn't the real sample
			 * count; the file isn't "tainted" because of it, so just print
			 * a warning if the amount of samples loaded wasn't what was expected */
			if (s > MAX_SAMPLES)
				return LOAD_UNSUPPORTED; /* punt */

			if (lflags & LOAD_NOSAMPLES) {
				s++;
				continue;
			}
			song_sample_t *sample = song->samples + s + 1;

			memcpy(sample->name, chunk.data->DSMP.name, 25);
			sample->name[25] = '\0';

			memcpy(sample->filename, chunk.data->DSMP.songname, 8);

			sample->length = bswapLE32(chunk.data->DSMP.length);
			sample->loop_start = bswapLE32(chunk.data->DSMP.loop_start);
			sample->loop_end = bswapLE32(chunk.data->DSMP.loop_end);
			sample->c5speed = bswapLE16(chunk.data->DSMP.c5speed);
			sample->volume = chunk.data->DSMP.volume * 4; // modplug

			csf_read_sample(sample, SF_LE | SF_8 | SF_M | SF_PCMD, chunk.data->DSMP.smp_bytes, chunk.data->DSMP.length);
			s++;
			break;
		}
	}

	for (n = nchn + 1; n < MAX_CHANNELS; n++)
		song->channels[n].flags |= CHN_MUTE;

	song->pan_separation = 128;
	song->flags = SONG_ITOLDEFFECTS | SONG_COMPATGXX;

	sprintf(song->tracker_id, "Protracker Studio module");

	return LOAD_SUCCESS;
}