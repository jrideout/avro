/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0 
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License. 
 */
#include <errno.h>
#include <assert.h>
#include <string.h>
#include "schema.h"
#include "datum.h"
#include "encoding.h"

static int
write_record(avro_writer_t writer, const avro_encoding_t * enc,
	     struct avro_record_schema_t *record, avro_datum_t datum)
{
	int rval;
	long i;

	for (i = 0; i < record->fields->num_entries; i++) {
		avro_datum_t field_datum;
		union {
			st_data_t data;
			struct avro_record_field_t *field;
		} val;
		st_lookup(record->fields, i, &val.data);
		rval = avro_record_get(datum, val.field->name, &field_datum);
		if (rval) {
			return rval;
		}
		rval = avro_write_data(writer, val.field->type, field_datum);
		if (rval) {
			return rval;
		}
	}
	return 0;
}

static int
write_enum(avro_writer_t writer, const avro_encoding_t * enc,
	   struct avro_enum_schema_t *enump, struct avro_enum_datum_t *datum)
{
	union {
		st_data_t data;
		long idx;
	} val;
	if (!st_lookup
	    (enump->symbols_byname, (st_data_t) datum->symbol, &val.data)) {
		return EINVAL;
	}
	return enc->write_long(writer, val.idx);
}

struct write_map_args {
	int rval;
	avro_writer_t writer;
	const avro_encoding_t *enc;
	avro_schema_t values_schema;
};

static int
write_map_foreach(char *key, avro_datum_t datum, struct write_map_args *args)
{
	int rval = args->enc->write_string(args->writer, key);
	if (rval) {
		args->rval = rval;
		return ST_STOP;
	}
	rval = avro_write_data(args->writer, args->values_schema, datum);
	if (rval) {
		args->rval = rval;
		return ST_STOP;
	}
	return ST_CONTINUE;
}

static int
write_map(avro_writer_t writer, const avro_encoding_t * enc,
	  struct avro_map_schema_t *writer_schema,
	  struct avro_map_datum_t *datum)
{
	int rval;
	struct write_map_args args = { 0, writer, enc, writer_schema->values };

	if (datum->map->num_entries) {
		rval = enc->write_long(writer, datum->map->num_entries);
		if (rval) {
			return rval;
		}
		st_foreach(datum->map, write_map_foreach, (st_data_t) & args);
	}
	if (!args.rval) {
		rval = enc->write_long(writer, 0);
		if (rval) {
			return rval;
		}
		return 0;
	}
	return args.rval;
}

static int
write_array(avro_writer_t writer, const avro_encoding_t * enc,
	    struct avro_array_schema_t *schema,
	    struct avro_array_datum_t *array)
{
	int rval;
	long i;

	if (array->els->num_entries) {
		rval = enc->write_long(writer, array->els->num_entries);
		if (rval) {
			return rval;
		}
		for (i = 0; i < array->els->num_entries; i++) {
			union {
				st_data_t data;
				avro_datum_t datum;
			} val;
			st_lookup(array->els, i, &val.data);
			rval =
			    avro_write_data(writer, schema->items, val.datum);
			if (rval) {
				return rval;
			}
		}
	}
	return enc->write_long(writer, 0);
}

static int
write_union(avro_writer_t writer, const avro_encoding_t * enc,
	    struct avro_union_schema_t *schema, avro_datum_t datum)
{
	int rval;
	long i;

	for (i = 0; i < schema->branches->num_entries; i++) {
		union {
			st_data_t data;
			avro_schema_t schema;
		} val;
		st_lookup(schema->branches, i, &val.data);
		if (avro_schema_datum_validate(val.schema, datum)) {
			rval = enc->write_long(writer, i);
			if (rval) {
				return rval;
			}
			return avro_write_data(writer, val.schema, datum);
		}
	}
	return EINVAL;
}

int
avro_write_data(avro_writer_t writer, avro_schema_t writer_schema,
		avro_datum_t datum)
{
	const avro_encoding_t *enc = &avro_binary_encoding;
	int rval = -1;

	if (!writer || !(is_avro_schema(writer_schema) && is_avro_datum(datum))) {
		return EINVAL;
	}
	if (!avro_schema_datum_validate(writer_schema, datum)) {
		return EINVAL;
	}
	switch (avro_typeof(writer_schema)) {
	case AVRO_NULL:
		rval = enc->write_null(writer);
		break;
	case AVRO_BOOLEAN:
		rval =
		    enc->write_boolean(writer, avro_datum_to_boolean(datum)->i);
		break;
	case AVRO_STRING:
		rval =
		    enc->write_string(writer, avro_datum_to_string(datum)->s);
		break;
	case AVRO_BYTES:
		rval =
		    enc->write_bytes(writer, avro_datum_to_bytes(datum)->bytes,
				     avro_datum_to_bytes(datum)->size);
		break;
	case AVRO_INT32:
		{
			int32_t i;
			if (is_avro_int32(datum)) {
				i = avro_datum_to_int32(datum)->i32;
			} else if (is_avro_int64(datum)) {
				i = (int32_t) avro_datum_to_int64(datum)->i64;
			} else {
				assert(0
				       &&
				       "Serious bug in schema validation code");
			}
			rval = enc->write_int(writer, i);
		}
		break;
	case AVRO_INT64:
		rval = enc->write_long(writer, avro_datum_to_int64(datum)->i64);
		break;
	case AVRO_FLOAT:
		{
			float f;
			if (is_avro_int32(datum)) {
				f = (float)(avro_datum_to_int32(datum)->i32);
			} else if (is_avro_int64(datum)) {
				f = (float)(avro_datum_to_int64(datum)->i64);
			} else if (is_avro_float(datum)) {
				f = avro_datum_to_float(datum)->f;
			} else if (is_avro_double(datum)) {
				f = (float)(avro_datum_to_double(datum)->d);
			} else {
				assert(0
				       &&
				       "Serious bug in schema validation code");
			}
			rval = enc->write_float(writer, f);
		}
		break;
	case AVRO_DOUBLE:
		{
			double d;
			if (is_avro_int32(datum)) {
				d = (double)(avro_datum_to_int32(datum)->i32);
			} else if (is_avro_int64(datum)) {
				d = (double)(avro_datum_to_int64(datum)->i64);
			} else if (is_avro_float(datum)) {
				d = (double)(avro_datum_to_float(datum)->f);
			} else if (is_avro_double(datum)) {
				d = avro_datum_to_double(datum)->d;
			} else {
				assert(0 && "Bug in schema validation code");
			}
			rval = enc->write_double(writer, d);
		}
		break;

	case AVRO_RECORD:
		rval =
		    write_record(writer, enc,
				 avro_schema_to_record(writer_schema), datum);
		break;

	case AVRO_ENUM:
		rval =
		    write_enum(writer, enc, avro_schema_to_enum(writer_schema),
			       avro_datum_to_enum(datum));
		break;

	case AVRO_FIXED:
		return avro_write(writer, avro_datum_to_fixed(datum)->bytes,
				  avro_datum_to_fixed(datum)->size);

	case AVRO_MAP:
		rval =
		    write_map(writer, enc, avro_schema_to_map(writer_schema),
			      avro_datum_to_map(datum));
		break;
	case AVRO_ARRAY:
		rval =
		    write_array(writer, enc,
				avro_schema_to_array(writer_schema),
				avro_datum_to_array(datum));
		break;

	case AVRO_UNION:
		rval =
		    write_union(writer, enc,
				avro_schema_to_union(writer_schema), datum);
		break;

	case AVRO_LINK:
		rval =
		    avro_write_data(writer,
				    (avro_schema_to_link(writer_schema))->to,
				    datum);
		break;
	}
	return rval;
}
