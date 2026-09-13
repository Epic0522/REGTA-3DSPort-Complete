/*
	getcpuflags_arm: get cpuflags for ARM

	copyright 1995-2014 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Taihei Momma
*/

#include <setjmp.h>
#include <signal.h>
#include "mpg123lib_intern.h"
#include "getcpuflags.h"

unsigned int getcpuflags(struct cpuflags* cf)
{
	cf->has_neon = 0;
	return 0;
}
