#!/bin/bash

. _settings.sh

SQL="
select t.table_name, c.column_name from information_schema.columns c
inner join information_schema.tables t on c.table_name = t.table_name and c.table_schema = t.table_schema
where c.table_schema = '$MYSQL_DATABASE'
and c.column_key = 'PRI' and c.extra = 'auto_increment';
"

echo "$SQL" | sudo mysql $MYSQL_CONNECTION_STRING | tail -n+2 | sed 's/[[:space:]]\+/ /g' | sort | uniq > identity_fields.txt
