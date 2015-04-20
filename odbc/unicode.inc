<?php

function unicode_x_encode($str) {
  $chars = preg_split('//u', $str, -1, PREG_SPLIT_NO_EMPTY);
  $encoded = '';
  foreach ($chars as $i => $ch) {
    $ord = ord($ch);

    $pak = unpack('H*', $ch);
    $codes = str_split($pak[1], 2);
    if (count($codes) == 1 && ($ch != '\\' || $chars[$i+1] == '_') && $ch != "\000") {
      $encoded .= $ch;
    } else {
      $enc = '\x'.implode('\x', $codes);
      $encoded .= $enc;
    }
  }

  return $encoded;
}

function unicode_x_decode($str) {
  $decoded = preg_replace_callback('/(\\\\x([0-9a-fA-F]{2}))+/', function ($match) {
    
    $codes = str_split($match[0], 4);
    $hex = '';
    foreach ($codes as $code) {
      $hex .= substr($c, 2);
    }
    // $codes = array_map(function ($c) {
    //   return substr($c, 2);
    // }, $codes);
    $pak = pack('H*', $hex);

    return $pak;
  }, $str);

  return $decoded;
}

function unicode_null_encode_decode($str) {
  return $str;
}