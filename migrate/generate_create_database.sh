#!/bin/bash

# Big ugly script for converting the MySQL database schema into a schema suitable
# for SQL Server.

. _settings.sh

CREATE_FILE=create_database.sql
IDENTITY_FILE=restore_primary_fields.sql
FIELDS_FILE=identity_fields.txt

mysqldump $MYSQL_CONNECTION_STRING --compatible=mssql --no-data --skip-comments > "$CREATE_FILE"

sed -i 's/ IF EXISTS//' "$CREATE_FILE"

sed -i 's/ COMMENT '"'"'.*'"'"'//g' "$CREATE_FILE"
sed -i 's/ bigint(20)/ bigint/' "$CREATE_FILE"
sed -i 's/ int(11)/ bigint/' "$CREATE_FILE"
sed -i 's/ int(10)/ bigint/' "$CREATE_FILE"
sed -i 's/ longblob/ varbinary(max)/' "$CREATE_FILE"
sed -i 's/ blob/ varbinary(max)/' "$CREATE_FILE"
sed -i 's/ longtext/ varchar(max)/' "$CREATE_FILE"
sed -i 's/ mediumtext/ varchar(max)/' "$CREATE_FILE"
sed -i 's/ text/ varchar(max)/' "$CREATE_FILE"
sed -i 's/ ntext/ varchar(max)/' "$CREATE_FILE"
sed -i 's/ nvarchar/ varchar/' "$CREATE_FILE"

sed -i 's/ tinyint([0-9])\?/ smallint/' "$CREATE_FILE"
sed -i 's/ smallint([0-9])\?/ smallint/' "$CREATE_FILE"
sed -i 's/ mediumint/ int/' "$CREATE_FILE"
sed -i 's/ int([0-9])\?/ int/' "$CREATE_FILE"
sed -i 's/ double/ float/' "$CREATE_FILE"

sed -i 's/ tinyint unsigned/ smallint/' "$CREATE_FILE"
# sed -i 's/ smallint unsigned/ mediumint/' "$CREATE_FILE"
# sed -i 's/ mediumint unsigned/ int/' "$CREATE_FILE"
sed -i 's/ unsigned//' "$CREATE_FILE"
sed -i 's/^ *\(UNIQUE\)\? KEY.*$//' "$CREATE_FILE"

sed -i 's/ CHARACTER SET utf8//' "$CREATE_FILE"
# sed -i 's/ COLLATE utf8_bin/ COLLATE SQL_Latin1_General_CP1_CI_AI/' "$CREATE_FILE"
# sed -i 's/ COLLATE utf8/ COLLATE SQL_Latin1_General_CP1_CI_AI/' "$CREATE_FILE"
sed -i 's/ COLLATE utf8_bin//' "$CREATE_FILE"
sed -i 's/ COLLATE utf8//' "$CREATE_FILE"


## fix for stupid drivers: turn all binary fields into strings
# sed -i 's/ varbinary/ varchar/' "$CREATE_FILE"

## fix for primary keys
sed -i 's/ AUTO_INCREMENT/ IDENTITY/' "$CREATE_FILE"

## General mssql compatibility
# sed -i 's/`/"/g' "$CREATE_FILE"
# sed -i 's/ ENGINE=InnoDB//' "$CREATE_FILE"
# sed -i 's/).*;/\);/' "$CREATE_FILE"



# copy the full SQL before we add extra stuff to it
CREATE_SQL="$(cat "$CREATE_FILE")"



# Work out the known tables by intersecting CREATE statements with identity keys

KNOWN_TABLES="$(cat "$FIELDS_FILE" | cut -d ' ' -f 1 | sort | uniq)"
CREATE_TABLES="$(cat "$CREATE_FILE" | grep -o 'CREATE TABLE ".*"' | cut -d '"' -f 2)"

THE_TABLES="$(comm -12 --nocheck-order <(echo "$KNOWN_TABLES") <(echo "$CREATE_TABLES"))"

for TABLE in $THE_TABLES; do
  TABLE_2="$TABLE"_2
  echo "if object_id('dbo.$TABLE_2', 'U') is not null drop table $TABLE_2;" >> "$CREATE_FILE"
done




##  Version 2

echo "print 'Creating new tables'" > "$IDENTITY_FILE"
echo "$CREATE_SQL" | sed "s/DROP TABLE \"\(.*\)\";/if object_id('\1_2', 'U') is not null exec('drop table \1_2;');/g" | sed 's/CREATE TABLE "\(.*\)"/CREATE TABLE "\1_2"/' >> "$IDENTITY_FILE"

# sed -i "/CREATE TABLE \"$TABLE_2\"/ { N ; /\"$FIELD\"/ s/,/ IDENTITY(1,1),/  }" "$IDENTITY_FILE"


##  add the IDENTITY flag to the required fields
while read -r LINE; do
  TABLE="$(echo "$LINE" | cut -d ' ' -f 1)"
  TABLE_2="$TABLE"_2
  FIELD="$(echo "$LINE" | cut -d ' ' -f 2)"
  VARIABLE="$TABLE"_"$FIELD"

  ## gods help this works somehow...
  # sed -i "/CREATE TABLE \"$TABLE_2\"/ { N ; /\"$FIELD\"/ s/,/ IDENTITY(1,1),/ }" "$IDENTITY_FILE"
  # sed -i "/CREATE TABLE \"$TABLE_2\"/ { N ; /\"$FIELD\"/ s/,/ IDENTITY(1,1),/ ; p ; d }" "$IDENTITY_FILE"
  # sed -i "/CREATE TABLE \"$TABLE_2\"/ { p ; N ; /\"$FIELD\"/ { s/,/ IDENTITY(1,1),/ } ; d }" "$IDENTITY_FILE"
  sed -i "/CREATE TABLE \"$TABLE_2\"/ { n ; /\"$FIELD\"/ !{ n } ; /\"$FIELD\"/ { s/,/ IDENTITY(1,1),/ } }" "$IDENTITY_FILE"
done < "$FIELDS_FILE"


echo "print 'Switching table schemas';" >> "$IDENTITY_FILE"
for TABLE in $THE_TABLES; do
  TABLE_2="$TABLE"_2
  echo "exec('alter table $TABLE switch to $TABLE_2; " >> "$IDENTITY_FILE"
  echo "drop table $TABLE; " >> "$IDENTITY_FILE"
  echo "exec sp_rename ''$TABLE_2'', ''$TABLE'';');" >> "$IDENTITY_FILE"
  echo >> "$IDENTITY_FILE"
done

echo "print 'Reseeding tables';" >> "$IDENTITY_FILE"
for TABLE in $THE_TABLES; do
  echo "dbcc checkident ($TABLE, RESEED);" >> "$IDENTITY_FILE"
done

## Clean up after yourself
for TABLE in $CREATE_TABLES; do
  TABLE_2="$TABLE"_2
  echo "if object_id('dbo.$TABLE_2', 'U') is not null drop table $TABLE_2;" >> "$IDENTITY_FILE"
done



## For convenience, also add a "drop everything" script

cat create_database.sql | grep -i 'drop table' > drop_database.sql