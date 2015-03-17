#!/usr/bin/env php
<?php

// $filename = trim(file_get_contents('php://stdin'))
$filename = $argv[1];
echo "Operating on file: $filename\n";
$sql = file_get_contents($filename);
$sql = preg_replace_callback('/CREATE TABLE .*?;/s', function ($str) {
    return "set @STMT = '".str_replace('\'', '\'\'', $str[0])."';\nEXEC(@STMT);";
  }, $sql);
file_put_contents($filename, $sql);