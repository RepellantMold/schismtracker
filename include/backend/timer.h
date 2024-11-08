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

#ifndef SCHISM_BACKEND_TIMER_H_
#define SCHISM_BACKEND_TIMER_H_

#include "headers.h"

#ifdef SCHISM_SDL2
/* SDL 2 has 64-bit ticks starting with 2.0.18 */
# define be_timer_ticks sdl2_timer_ticks
# define be_timer_ticks_passed sdl2_timer_ticks_passed
# define be_delay sdl2_delay
typedef uint64_t schism_ticks_t;
#elif SCHISM_SDL12
# define be_timer_ticks sdl12_timer_ticks
# define be_timer_ticks_passed sdl12_timer_ticks_passed
# define be_delay sdl12_delay
typedef uint32_t schism_ticks_t;
#endif

schism_ticks_t sdl2_timer_ticks(void);
schism_ticks_t sdl12_timer_ticks(void);

int sdl2_timer_ticks_passed(schism_ticks_t a, schism_ticks_t b);
int sdl12_timer_ticks_passed(schism_ticks_t a, schism_ticks_t b);

void sdl2_delay(uint32_t ms);
void sdl12_delay(uint32_t ms);

#endif /* SCHISM_BACKEND_TIMER_H_ */
