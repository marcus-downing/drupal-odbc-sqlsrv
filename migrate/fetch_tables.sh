#!/bin/bash

. _settings.sh

echo "show tables" | sudo mysql $MYSQL_CONNECTION_STRING | tail -n+2 | sort | uniq > tables.txt