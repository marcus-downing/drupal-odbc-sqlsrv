<?php

function unicode_x_encode($str) {
  if (!preg_match('/[^ -~]/', $str))
    return $str;

  $chars = preg_split('//u', $str, -1, PREG_SPLIT_NO_EMPTY);
  $encoded = '';
  reset($chars);
  foreach ($chars as $i => $ch) {
    $pak = unpack('H*', $ch);
    $codes = str_split($pak[1], 2);
    if (count($codes) == 1 && ($ch != '\\' || $chars[$i+1] == '_') && $ch != "\000" && $ch != "\r" && $ch != "\n") {
      $encoded .= $ch;
    } else {
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