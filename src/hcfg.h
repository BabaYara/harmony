/*
 * Copyright 2003-2015 Jeffrey K. Hollingsworth
 *
 * This file is part of Active Harmony.
 *
 * Active Harmony is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Active Harmony is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Active Harmony.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __HCFG_H__
#define __HCFG_H__

#include "defaults.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hcfg_info {
    const char* key;
    const char* val;
    const char* help;
} hcfg_info_t;
extern const hcfg_info_t hcfg_global_keys[];

typedef struct hcfg {
    char** env;
    int    len;
    int    cap;
} hcfg_t;
extern const hcfg_t HCFG_INITIALIZER;

int    hcfg_init(hcfg_t* cfg);
int    hcfg_reginfo(hcfg_t* cfg, const hcfg_info_t* info);
int    hcfg_copy(hcfg_t* dst, const hcfg_t* src);
void   hcfg_fini(hcfg_t* cfg);

char*  hcfg_get(const hcfg_t* cfg, const char* key);
int    hcfg_bool(const hcfg_t* cfg, const char* key);
long   hcfg_int(const hcfg_t* cfg, const char* key);
double hcfg_real(const hcfg_t* cfg, const char* key);

int    hcfg_arr_len(const hcfg_t* cfg, const char* key);
int    hcfg_arr_get(const hcfg_t* cfg, const char* key, int idx,
                    char* buf, int len);
int    hcfg_arr_bool(const hcfg_t* cfg, const char* key, int idx);
long   hcfg_arr_int(const hcfg_t* cfg, const char* key, int idx);
double hcfg_arr_real(const hcfg_t* cfg, const char* key, int idx);

int    hcfg_set(hcfg_t* cfg, const char* key, const char* val);
int    hcfg_loadfile(hcfg_t* cfg, const char* filename);
int    hcfg_write(const hcfg_t* cfg, const char* filename);

int    hcfg_serialize(char** buf, int* buflen, const hcfg_t* cfg);
int    hcfg_deserialize(hcfg_t* cfg, char* buf);

#ifdef __cplusplus
}
#endif

#endif
