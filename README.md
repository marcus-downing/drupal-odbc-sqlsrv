
# ODBC driver for Drupal 7 and SQL Server

This is a fork of [the ODBC Driver for Drupal 7](https://www.drupal.org/sandbox/pstewart/2010758). This fork uses precomposed queries to work around a bug in SQL Server 2008 R2 and earlier that breaks prepared statements on ODBC.


## Why?

Good question. The preferred way of running Drupal on SQL Server is to use [the sqlsrv driver](https://www.drupal.org/project/sqlsrv) and run the web servers on Windows.

But if for some reason you need to run the web server on Linux, Mac or something else but talk to a SQL Server database running on Windows, this driver can help.

Beware that this way of connecting has problems with binary data stored as `varchar(max)` and `varbinary(max)`.
See below for details on converting fields to a format that works.

Beware also that performance of this driver will never be top notch.
This is due to the way prepared statements are precomposed and the number of layers the query has to go through.
I recommend [using Memcache for Drupal](http://andrewdunkle.com/2012/how-to-install-memcached-for-drupal-7.html) to improve performance.


## Installation

### Prerequisites

To use this driver, you'll first need to install these components:

 * [Microsoft® SQL Server® ODBC Driver 1.0 for Linux](http://www.microsoft.com/en-gb/download/details.aspx?id=28160)
 * [odbcUNIX](http://www.unixodbc.org/)
 * PHP 5.3 or above
 * Drupal 7, obviously

Note that the Microsoft driver is built with Red Hat Linux, and [may require extra work to install on other versions](https://blog.afoolishmanifesto.com/posts/install-and-configure-the-ms-odbc-driver-on-debian/).

FreeTDS is an alternative to the Microsoft driver. See notes near the bottom.

### ODBC settings

Edit your ODBC instance settings, which are probably at `/etc/odbcinst.ini`.

```ini
[SQL Server Native Client 11.0]
Description     = Microsoft SQL Server ODBC Driver V1.0 for Linux
Driver          = /opt/microsoft/sqlncli/lib64/libsqlncli-11.0.so.1790.0
UsageCount      = 1
Threading       = 1
Trace           = Yes
TraceFile       = /var/log/mssqlsrvodbc.log
ForceTrace      = Yes
```

Check the location of the `Driver` value for yourself, as it may be different. Note that you **must include the spaces** in the above file, otherwise it won't work.

Be aware also that this driver **does not** use the connections you define in `/etc/odbc.ini`, which is why the connection settings need to use the full hostname and port, not a short name.


### This module

Database connectors behave a little differently from other modules.
Copy the `odbc` directory from inside this module and put it into your Drupal installation's `includes/database` directory.


### Drupal settings

You'll need to put a config like this in your settings file (probably something like `sites/default/settings.local.php`):

```php
<?php

$databases['default']['default'] = array(
  'driver'        => 'odbc',
  'odbc_driver'   => 'SQL Server Native Client 11.0',
  'host'          => '<hostname>',
  'port'          => 1433,
  'database'      => '<database name>',
  'username'      => '<username>',
  'password'      => '<password>',
  'prefix'        => '',
);
```

Note that the value of `odbc_driver` must match up to the name you gave it in the ODBC settings. The name `'SQL Server Native Client 11.0'` should be the default when the Microsoft driver is installed.


## Alternatively, FreeTDS

[FreeTDS](http://www.freetds.org/) is an open source Linux driver for SQL Server and Sybase databases.
This module has not been thoroughly tested on FreeTDS, so you are likely to encounter errors.

### ODBC settings

Put a settings block for FreeTDS into your ODBC instance settings (probably `/etc/odbcinst.ini`).

```ini
[FreeTDS]
Description     = FreeTDS
Driver          = /usr/lib64/libtdsodbc.so
Setup           = /usr/lib64/libtdsS.so.2
Trace           = Yes
TraceFile       = /var/log/freetdsodbc.log
ForceTrace      = Yes
UsageCount      = 1
```

Again, check the precise location of the library files yourself. Again, you must include the spaces.

### Drupal config

```php
<?php

$databases['default']['default'] = array(
  'driver'        => 'odbc',
  'odbc_driver'   => 'FreeTDS',
  'tds_version'   => '8.0'
  'host'          => '<hostname>',
  'port'          => 1433,
  'database'      => '<database name>',
  'username'      => '<username>',
  'password'      => '<password>',
  'prefix'        => '',
);
```

Note the addition of the `tds_version` field.


## Binary data

The Microsoft driver for Linux has a problem with binary data at maximum size.
It copes fine up to the highest fixed size (`varchar(8000)`, `nvarchar(8000)` and `varbinary(8000)`), but chokes on unlimited size fields (`varchar(max)`, `nvarchar(max)` and `varbinary(max)`).
I've found it works significantly better if these fields are converted either to `text` if they need to be massive, or a `varchar` or limited size if they need to be used as keys.


### Converting an existing database

Depending what modules you have install, Drupal uses a lot of tables. Copying data from MySQL to SQL Server is less than obvious.

In the `convert/` directory of this module you'll find a number of scripts for reading settings from a MySQL database and writing SQL scripts to import them into a SQL Server database.
The actual data is copied using ODBC, which you must enable on your MySQL server first.

**TODO -- actually put these files in**
