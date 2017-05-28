/**
 * Copyright (C) 2017  Sergey Morozov <sergey94morozov@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CLOUDTIERING_SCAN_H
#define CLOUDTIERING_SCAN_H

/*******************************************************************************
* FILE SYSTEM SCANNER                                                          *
* ------                                                                       *
* TODO: write description                                                      *
*******************************************************************************/

#include "queue.h"

void file_system_scanner(queue_t *demotion_queue, queue_t *promotion_queue);

#endif    /* CLOUDTIERING_SCAN_H */