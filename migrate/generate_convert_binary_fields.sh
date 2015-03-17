#!/bin/bash

# Fix for stupid MS SQL ODBC drivers that screw up binary fields
# Convert all varbinary into varchar

FIELDS_FILE=binary_fields.txt
OUTFILE=convert_binary_fields.sql

echo > "$OUTFILE"

while read -r LINE; do
  TABLE="$(echo "$LINE" | cut -d ' ' -f 1)"
  FIELD="$(echo "$LINE" | cut -d ' ' -f 2)"
  FIELD_2="$FIELD"_2
  TYPE="$(echo "$LINE" | cut -d ' ' -f 3-)"
  CONV_TYPE="$(echo "$TYPE" | sed 's/ not null//' | sed "s/ default ''//" | sed "s/ default 0x//")"
  INTERMEDIATE_TYPE="varchar(max)"
  if echo "$CONV_TYPE" | grep 'varbinary' > /dev/null; then
    INTERMEDIATE_TYPE='varbinary(max)'
  fi

  echo "print 'Converting $TABLE.$FIELD to $CONV_TYPE';" >> "$OUTFILE"
  echo "begin try" >> "$OUTFILE"

  echo "IF exists( select * from INFORMATION_SCHEMA.COLUMNS where TABLE_NAME='$TABLE' and COLUMN_NAME='$FIELD_2') alter table $TABLE drop column $FIELD_2;" >> "$OUTFILE"
  echo "alter table $TABLE add $FIELD_2 $TYPE;" >> "$OUTFILE"
  echo "exec('update $TABLE set $FIELD_2 = CAST(CAST($FIELD as $INTERMEDIATE_TYPE) as $CONV_TYPE)');" >> "$OUTFILE"
  echo "exec('alter table $TABLE drop column $FIELD');" >> "$OUTFILE"
  echo "exec sp_rename '$TABLE.$FIELD_2', '$FIELD', 'column';" >> "$OUTFILE"

  echo "end try" >> "$OUTFILE"
  echo "begin catch" >> "$OUTFILE"
  echo "end catch" >> "$OUTFILE"

  echo  >> "$OUTFILE"
done < "$FIELDS_FILE"

echo "print 'Looking for unconverted binary fields...';" >> "$OUTFILE"
echo "select tables.name as table_name, columns.name as column_name, types.name as type_name from sys.columns inner join sys.tables on columns.object_id = tables.object_id inner join sys.types on columns.system_type_id = types.system_type_id where columns.max_length = -1 and columns.system_type_id in (165,167,231);" >> "$OUTFILE"