#!/bin/bash

# To use this script, you must first make your MySQL Database available remotely,
# and set up an ODBC data source for it in your SQL Server Management Console.

. _settings.sh

echo -n "" > insert_select.sql

for T in `cat tables.txt | grep -v '^#'`; do 
  echo "print 'Clearing $T';" >> insert_select.sql
  echo "delete from $T;" >> insert_select.sql
  echo "print 'Importing $T';" >> insert_select.sql
  echo "insert into $T select * from openquery($MYSQL_DATABASE, 'select * from $T');" >> insert_select.sql
  echo "" >> insert_select.sql
done
