/*
 * table-detector.h
 *
 *  Created on: Jan 4, 2022
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_TABLE_DETECTOR_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_TABLE_DETECTOR_H_

#include "TableRegion.h"

TableList* detectTables(SvgBuilder *builder, TableList* tables);


#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_TABLE_DETECTOR_H_ */
