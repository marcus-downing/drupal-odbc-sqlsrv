<?php

function unicode_x_encode($str) {
  $chars = preg_split('//u', $str, -1, PREG_SPLIT_NO_EMPTY);
  $encoded = '';
  foreach ($chars as $ch) {
    // $enc = dechex(ord($ch));
    $ord = ord($ch);

    // $ucs = mb_convert_encoding($ch, 'UCS-2BE', 'UTF-8');
    // $ucs = mb_convert_encoding($ch, 'UCS-4', 'UTF-8');
    $pak = unpack('H*', $ch);
    $codes = str_split($pak[1], 2);
    if (count($codes) == 1 && $ch != '\\') {
      $encoded .= $ch;
    } else {
      $enc = '\x'.implode('\x', $codes);
      // echo "$ord $enc $ch\n";
      $encoded .= $enc;
    }
  }

  return $encoded;
}

function unicode_x_decode($str) {
  $decoded = preg_replace_callback('/(\\\\x([0-9a-fA-F]{2}))+/', function ($match) {
    
    $codes = str_split($match[0], 4);
    $codes = array_map(function ($c) {
      return substr($c, 2);
    }, $codes);
    // print_r($codes);
    // echo "\n";
    $pak = pack('H*', implode('', $codes));

    // $pak = implode('', array_map(function ($m) {
    //   $codes = array_map(function ($x) {
    //     return substr($x, 2);
    //   }, str_split($m, 4));
    //   print_r($codes); echo " $m\n";
    //   return pack('H*', implode('', $codes));
    // }, $match));

    // $pak = pack('H*', $match[1]);
    // $dec = mb_convert_encoding($pak, 'UTF-8', 'UCS-2BE');
    // echo "$pak $dec\n";
    return $pak;
  }, $str);

  // $decoded = preg_replace_callback('/\\\\u([0-9a-fA-F]{4})/', function ($match) {
  //   return mb_convert_encoding(pack('H*', $match[1]), 'UTF-8', 'UCS-2BE');
  // }, $str); 
  // $decoded = preg_replace_callback('/(\u[0-9A-Fa-f]{4})+/', function($matches) {

  //   return '';
  // }, $str);
  return $decoded;
}