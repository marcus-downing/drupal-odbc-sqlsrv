<?php
// $Id$

// Microsoft SQL Server specific install functions

class DatabaseTasks_sqlsrv extends DatabaseTasks {
  protected $pdoDriver = 'sqlsrv';
  public function name() {
    return 'Microsoft SQL Server';
  }

  public function __construct() {
    $this->tasks[] = array(
      'function' => 'initializeDatabase',
      'arguments' => array(),
    );
  }

  /**
   * Make SQL Server Drupal friendly.
   */
  function initializeDatabase() {
    try {
      // SUBSTRING_INDEX() function.
      db_query(<<< EOF
IF OBJECT_ID (N'SUBSTRING_INDEX', N'FN') IS NOT NULL BEGIN DROP FUNCTION SUBSTRING_INDEX END
EOF
      );
      db_query(<<< EOF
CREATE FUNCTION SUBSTRING_INDEX(@string varchar(8000), @delimiter char(1), @count int) RETURNS varchar(8000) AS
BEGIN
  DECLARE @result varchar(8000)
  DECLARE @end int
  DECLARE @part int
  SET @end = 1
  SET @part = 0
  WHILE (@part < @count)
  BEGIN
    SET @end = CHARINDEX(@delimiter, @string, @end + 1)
    IF (@end = 0)
    BEGIN
      SET @end = LEN(@string)
      BREAK
    END
    SET @count = @count + 1
  END
  SET @result = SUBSTRING(@string, 1, @end)
  RETURN @result
END
EOF
      );

      // GREATEST() function.
      db_query(<<< EOF
IF OBJECT_ID (N'GREATEST', N'FN') IS NOT NULL BEGIN DROP FUNCTION GREATEST END
EOF
      );
      db_query(<<< EOF
CREATE FUNCTION GREATEST(@op1 real, @op2 real) RETURNS real AS
BEGIN
  DECLARE @result real
  SET @result = CASE WHEN @op1 >= @op2 THEN @op1 ELSE @op2 END
  RETURN @result
END
EOF
      );

      // CONCAT() function.
      db_query(<<< EOF
IF OBJECT_ID (N'CONCAT', N'FN') IS NOT NULL BEGIN DROP FUNCTION CONCAT END
EOF
      );
      db_query(<<< EOF
CREATE FUNCTION CONCAT(@op1 sql_variant, @op2 sql_variant) RETURNS nvarchar(max) AS
BEGIN
  DECLARE @result nvarchar(max)
  SET @result = CAST(@op1 AS nvarchar(max)) + CAST(@op2 AS nvarchar(max))
  RETURN @result
END
EOF
      );

      // IF(expr1, expr2, expr3) function.
      db_query(<<< EOF
IF OBJECT_ID (N'IF', N'FN') IS NOT NULL BEGIN DROP FUNCTION [IF] END
EOF
      );
      db_query(<<< EOF
CREATE FUNCTION [IF](@expr1 sql_variant, @expr2 sql_variant, @expr3 sql_variant) RETURNS sql_variant AS
BEGIN
  DECLARE @result sql_variant
  SET @result = CASE WHEN CAST(@expr1 AS int) != 0 THEN @expr2 ELSE @expr3 END
  RETURN @result
END
EOF
      );
    }
    catch (Exception $e) {
      print_r($e);
      $this->fail(st('Drupal could not be correctly setup with the existing database. Revise any errors.'));
    }
  }
}
