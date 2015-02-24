
# ODBC driver for Drupal 7, forked for SQL Server connection

This is a fork of the ODBC Driver for Drupal 7. This fork uses precomposed queries to work around a bug in SQL Server 2008 R2 and earlier that breaks prepared statements on ODBC.

Beware that this way of connecting has problems with binary data. We recommend all `varbinary(max)` and `varchar(max)` fields get turned into `text`.

See https://drupal.org/project/2010758/ for installation instructions.
