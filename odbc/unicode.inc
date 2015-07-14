<?php

function unicode_x_encode($str) {
  if (!preg_match('/[^ -~]/', $str))
    return $str;

  $parts = preg_split('/([ -~]+)/u', $str, -1, PREG_SPLIT_NO_EMPTY | PREG_SPLIT_DELIM_CAPTURE);
  $encoded = '';
  reset($parts);
  foreach ($parts as $part) {
    if (!preg_match('/[^ -~]/', $part)) {
      $encoded .= $part;
    } else {
      $pak = unpack('H*', $part);
      $codes = str_split($pak[1], 2);
      reset($codes);
      foreach ($codes as $code) {
        $encoded .= '\x'.$code;
      }
    }
  }
  return $encoded;
}

function unicode_x_decode($str) {
  $decoded = preg_replace_callback('/(\\\\x([0-9a-fA-F]{2}))+/', function ($match) {
    
    $codes = str_split($match[0], 4);
    $hex = '';
    reset($codes);
    foreach ($codes as $code) {
      $hex .= substr($code, 2);
    }
    $pak = pack('H*', $hex);

    return $pak;
  }, $str);

  return $decoded;
}

function unicode_null_encode_decode($str) {
  return $str;
}