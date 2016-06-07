<?php

/**
 * @file
 * Database interface code for ODBC PDO driver.
 */

/**
 * @ingroup database
 * @{
 */

include_once DRUPAL_ROOT . '/includes/database/prefetch.inc';
include_once __DIR__ . '/statement.inc';
include_once __DIR__ . '/unicode.inc';

// can't always take constants for granted across PHP versions, so define these if they're not already
if (!defined('WATCHDOG_CRITICAL'))  define('WATCHDOG_CRITICAL', 2);
if (!defined('WATCHDOG_ERROR'))     define('WATCHDOG_ERROR', 3);
if (!defined('WATCHDOG_WARNING'))   define('WATCHDOG_WARNING', 4);
if (!defined('WATCHDOG_NOTICE'))    define('WATCHDOG_NOTICE', 5);
if (!defined('WATCHDOG_INFO'))      define('WATCHDOG_INFO', 6);
if (!defined('WATCHDOG_DEBUG'))     define('WATCHDOG_DEBUG', 7);

if (!defined('ENT_HTML401'))        define('ENT_HTML401', 0);
if (!defined('ENT_HTML5'))          define('ENT_HTML5', 16 | 32);
if (!defined('ENT_NOQUOTES'))       define('ENT_NOQUOTES', 0);
if (!defined('ENT_IGNORE'))         define('ENT_IGNORE', 4);


/**
 * Swallow some exceptions and log the errors instead
 */
if (!defined('ODBC_FORGIVING')) define('ODBC_FORGIVING', true);

/**
 * Whether to use transactions with the SQL connection.
 *
 * This can be overridden in the connection settings with 'transations' => (boolean)
 */
if (!defined('ODBC_USE_TRANSACTIONS')) define('ODBC_USE_TRANSACTIONS', false);

/**
 * Whether to falsify the behaviour of temporary tables
 *
 */
if (!defined('ODBC_FAUX_TEMPORARY_TABLES')) define('ODBC_FAUX_TEMPORARY_TABLES', false);

/**
 * Use the deprecated but more compatible `text` data type in place of `varchar` and `varbinary`
 *
 * This can be overridden in the connection setttings with 'use_text' => (boolean)
 */
if (!defined('ODBC_USE_TEXT')) define('ODBC_USE_TEXT', true);

/**
 * Use this encoding to store Unicode text on a non-Unicode database.
 *
 *  ''                  = No encoding, use UTF-8 directly          £
 *  'html'              = HTML character entities                  &#163;
 *  'u'                 = Unicode escaped characters               \u00A3
 *  'url'               = URL-encoded characters                   %C2%A3
 *  'quoted-printable'  = Use email "quoted-printable" encoding    =C2=A3
 *
 * This can be overridden in the connection settings with 'encoding' => (string)
 */
if (!defined('ODBC_ENCODING')) define('ODBC_ENCODING', 'u');

/**
 * Deeper debugging. Do not leave this flag switch on!
 *
 * For debugging of your own code, set 'debug' => true flag on the database connection settings.
 *
 * 0 = None
 * 1 = Lots of debugging spewed all over your screen
 * 2 = A totally unreasonable amount of debugging
 */
if (!defined('ODBC_LOGGING_DEBUG')) define('ODBC_LOGGING_DEBUG', 0);
if (!defined('ODBC_HEX_DEBUG')) define('ODBC_HEX_DEBUG', 0);
if (!defined('ODBC_LOG_COLOUR')) define('ODBC_LOG_COLOUR', false);



class DatabaseConnection_odbc extends DatabaseConnection {

  public $bypassQueryPreprocess = false;

  private $debug = false;
  private $log_threshold = WATCHDOG_WARNING;
  private $logto = false;

  private $temporaryTables = array();

  // private $cursorDepth = 0;
  // private $watchdogQueue = array();

  // public $drush;

  /**
   * Override of DatabaseConnection::driver().
   *
   * @status tested
   */
  public function driver() {
    return 'odbc';
  }

  /**
   * Override of DatabaseConnection::databaseType().
   *
   * @status tested
   */
  public function databaseType() {
    return 'odbc';
  }

  /**
   * Override of DatabaseConnection::version().
   */
  public function version() {
    $version = $this->query('SELECT @@VERSION')->fetchField();
    $this->log('Server version: %version', array('%version' => $version), WATCHDOG_INFO);
    return $version;
  }

  /**
   * Log messages with dblog; or queue them if we're in the middle of another query
   */
  public function log($message, $variables = array(), $severity = WATCHDOG_NOTICE, $link = '') {
    if (!$this->debug && $severity > WATCHDOG_ERROR)
      return;

    $threshold = $this->log_threshold;
    if (isset($variables['threshold'])) {
      $threshold = intval($variables['threshold']);
      unset($variables['threshold']);
    }
    if ($severity > $threshold)
      return;

    $prefix = '';
    if (isset($variables['this'])) {
      $prefix = '['.substr(md5(spl_object_hash($variables['this'])), 0, 4).'] ';
      unset($variables['this']);
    }

    if (ODBC_LOGGING_DEBUG) {
      echo "<p><b>ODBC Log to $this->logto:</b> $message</p>\n<pre>"; print_r($variables); echo "</pre>\n";
    }

    switch ($this->logto) {
      case 'echo':
        $sevc = '';
        $reset = '';
        $magenta = '';
        if (ODBC_LOG_COLOUR && function_exists('bang_syslog_colors')) {
          extract(bang_syslog_colors());

          switch ($severity) {
            case WATCHDOG_CRITICAL:
            case WATCHDOG_ERROR:
              $sevc = $red;
              break;

            case WATCHDOG_WARNING:
              $sevc = $yellow;
              break;

            case WATCHDOG_NOTICE:
              $sevc = $cyan;
              break;

            case WATCHDOG_INFO:
            case WATCHDOG_DEBUG:
              $sevc = $blue;
              break;
          }
        }
        $message = $this->logReplacePlaceholders($message, $variables);

        $sev = $this->logSeverity($severity);
        echo "$sevc$prefix($sev) $message$reset";
        if (!empty($variables)) {
          echo "    $magenta";
          print_r($variables);
        }
        echo "\n";
        if ($severity <= WATCHDOG_ERROR)
          debug_print_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS);
        break;

      case 'drush':
        $message = $this->logReplacePlaceholders($message, $variables);
        $sev = $this->logSeverity($severity);

        if (!empty($variables))
          $message .= '  '.print_r($variables, true);
        drush_log("$prefix($sev) $message", 'notice');
        break;

      case 'syslog':
        $message = $this->logReplacePlaceholders($message, $variables);
        // bang_syslog('odbc-sqlsrv: '.$prefix.$message);
        if (empty($variables))
          bang_syslog('odbc-sqlsrv: '.$prefix.$message);
        else
          bang_syslog('odbc-sqlsrv: '.$prefix.$message, $variables);
        break;

      case 'watchdog':
        if (is_array($variables))
          $variables = array_map(function ($var) {
            if (is_array($var) || is_object($var))
              $var = print_r($var, true);
            return $var;
          }, $variables);
        watchdog('odbc-sqlsrv', $prefix.$message, $variables, $severity, $link);
        break;
    }
  }

  private function hex_dump($data, $newline="\n") {
    static $from = '';
    static $to = '';

    static $width = 16; # number of bytes per line

    static $pad = '.'; # padding for non-visible characters

    if ($from==='') {
      for ($i = 0; $i <= 0xFF; $i++) {
        $from .= chr($i);
        $to .= ($i >= 0x20 && $i <= 0x7E) ? chr($i) : $pad;
      }
    }

    $hex = str_split(bin2hex($data), $width*2);
    $chars = str_split(strtr($data, $from, $to), $width);

    $offset = 0;
    $w = $width * 3 - 1;
    reset($hex);
    foreach ($hex as $i => $line) {
      $hex = implode(' ', str_split($line,2));
      echo sprintf('%6X',$offset).' : '.sprintf('%-'.$w.'s', $hex). ' [' . $chars[$i] . ']' . $newline;
      $offset += $width;
    }
    echo $newline;
  }

  private function logReplacePlaceholders($message, &$variables) {
    $replacable = array();
    reset($variables);
    foreach ($variables as $key => $value) {
      if (is_array($value) && empty($value))
        $variables[$key] = '[]';
      else if (is_object($value) || is_array($value))
        continue;

      $replacable[$key] = $value;
      // unset($variables[$key]);
    }
    $replaced_keys = array();
    $new_message = $this->replacePlaceholders($message, $replacable, false, $replaced_keys);
    reset($replaced_keys);
    foreach ($replaced_keys as $key)
      unset($variables[$key]);

    if (count($variables) == 1) {
      if (isset($variables['%args']))
        $variables = $variables['%args'];
      else if (isset($variables['%values']))
        $variables = $variables['%values'];
    }

    return $new_message;
  }

  private function logSeverity($severity) {
    switch ($severity) {
      case WATCHDOG_CRITICAL: return 'CRITICAL';
      case WATCHDOG_ERROR: return 'ERROR';
      case WATCHDOG_WARNING: return 'Warning';
      case WATCHDOG_NOTICE: return 'Notice';
      case WATCHDOG_INFO: return 'Info';
      case WATCHDOG_DEBUG: return 'Debug';
      default: return '';
    }
  }

  private function severityThreshold($severity) {
    switch ($severity) {
      case 'critical': return WATCHDOG_CRITICAL;
      case 'error': return WATCHDOG_ERROR;
      case 'warning': return WATCHDOG_WARNING;
      case 'notice': return WATCHDOG_NOTICE;
      case 'info': return WATCHDOG_INFO;
      case 'debug': return WATCHDOG_DEBUG;
      default: return WATCHDOG_WARNING;
    }
  }


  /**
   * Internal method to artificially load the syslog module before the database is initialised
   */
  private function loadSyslog() {
    global $odbc_sqlsrv_syslog;
    if (!function_exists('bang_syslog') && !$odbc_sqlsrv_syslog) {
      if (ODBC_LOGGING_DEBUG) echo "loading syslog\n";
      $odbc_sqlsrv_syslog = true;

      if (!function_exists('drupal_system_listing')) {
        require_once DRUPAL_ROOT . '/includes/common.inc';
      }

      if (!defined('SYSLOG_COLORS')) define('SYSLOG_COLORS', true);
      if (!defined('SYSLOG_DISABLE')) define('SYSLOG_DISABLE', false);
      if (!defined('SYSLOG_ESCAPE_NEWLINES')) define('SYSLOG_ESCAPE_NEWLINES', true);
      if (!isset($_SERVER['SERVER_NAME'])) $_SERVER['SERVER_NAME'] = '';

      //  first load WordPress
      $this->loadModule('wordpress');
      $this->loadModule('bang_syslog');

      // if (ODBC_LOGGING_DEBUG) echo "looking for WordPress module\n";
      // $likely_wp =
      // if (file_exists(realp))
      // $matches = drupal_system_listing('/^wordpress\.module$/', 'modules', 'name', 0);
      // reset($matches);
      // foreach ($matches as $file) {
      //   $path = realpath($file->uri);
      //   if (ODBC_LOGGING_DEBUG) echo "$path\n";
      //   if (file_exists($path)) {
      //     include_once($path);
      //     break;
      //   }
      // }

      // //  then Syslog
      // if (ODBC_LOGGING_DEBUG) echo "looking for Syslog module\n";
      // $matches = drupal_system_listing('/^bang_syslog\.module$/', 'modules', 'name', 0);
      // reset($matches);
      // foreach ($matches as $file) {
      //   $path = realpath($file->uri);
      //   if (ODBC_LOGGING_DEBUG) echo "$path\n";
      //   if (file_exists($path)) {
      //     include_once($path);
      //     break;
      //   }
      // }
    }

    if (ODBC_LOGGING_DEBUG) echo "syslog init\n";
    if (function_exists('bang_syslog__init')) {
      bang_syslog__init();
      return true;
    }
    return false;
  }

  private function loadModule($module) {
    $likely = array(
      'sites/all/modules/contrib/'.$module.'/'.$module.'.module'
    );
    foreach ($likely as $file) {
      $path = realpath($file);
      if (ODBC_LOGGING_DEBUG) echo "$path\n";
      if (file_exists($path)) {
        include_once($path);
        return;
      }
    }


    if (ODBC_LOGGING_DEBUG) echo "Searching for $module module\n";
    $matches = drupal_system_listing('/^'.$module.'\.module$/', 'modules', 'name', 0);
    reset($matches);
    foreach ($matches as $file) {
      $path = realpath($file->uri);
      if (ODBC_LOGGING_DEBUG) echo "$path\n";
      if (file_exists($path)) {
        include_once($path);
        return;
      }
    }
  }

  public function __construct(array $connection_options = array()) {
    // Whether to use dblog / watchdog / syslog / drush to record debug information
    $this->debug = isset($connection_options['debug']) && (boolean) $connection_options['debug'];
    if ($this->debug) {
      $syslog_ok = $this->loadSyslog();
      $watchdog_ok = function_exists('watchdog');
      $this->log_threshold = $this->severityThreshold(isset($connection_options['log_threshold']) ? $connection_options['log_threshold'] : 'warning');

      if (defined('DRUSH_BASE_PATH') || php_sapi_name() == 'cli') $this->logto = 'echo';
      else if ($syslog_ok) $this->logto = 'syslog';
      else if ($watchdog_ok) $this->logto = 'watchdog';

      if (ODBC_LOGGING_DEBUG) {
        echo "<p>Can syslog? ".$syslog_ok."</p>\n";
        echo "<p>Can watchdog? ".$watchdog_ok."</p>\n";
        echo "<p><b>ODBC Logging to $this->logto</b>\n";
      }
    }

    // Store connection options for future reference.
    $this->connectionOptions = $connection_options;

    // We don't need a specific PDOStatement class here, we simulate it using
    // DatabaseStatement_odbc below.
    $this->statementClass = null;

    // Whether to use transactions
    $this->transactionSupport = isset($connection_options['transactions']) ? (boolean) $connection_options['transactions'] : ODBC_USE_TRANSACTIONS;

    // Whether to falsify the behaviour of temporary tables
    $this->fauxTemporaryTables = isset($connection_options['faux_temporary_tables']) ? (boolean) $connection_options['faux_temporary_tables'] : ODBC_FAUX_TEMPORARY_TABLES;

    // Whether to use `text` fields instead of varchar(max) and varbinary(max)
    $this->useTextFields = isset($connection_options['use_text']) ? (boolean) $connection_options['use_text'] : ODBC_USE_TEXT;

    // The encoding to use for higher characters
    $this->encoding = isset($connection_options['encoding']) ? $connection_options['encoding'] : ODBC_ENCODING;

    // How to encode unicode
    if ($this->encoding == 'u') {
      $this->encode = 'unicode_x_encode';
      $this->decode = 'unicode_x_decode';
    } else if ($this->encoding == 'url') {
      $this->encode = 'urlencode';
      $this->decode = 'urldecode';
    } else {
      $this->encode = 'odbc_null_encode_decode';
      $this->decode = 'odbc_null_encode_decode';
    }

    // Build the DSN.
    // TODO: Make this work with a named DSN
    $options = array();
    if (isset($connection_options['host'])) {
      if (strpos($connection_options['host'], ';server=') !== false) {
        // compatibility mode - the config is using the workaround for Drush, so leave it alone
        $options[] = 'host='.$connection_options['host'];
      } else {
        // normal mode - build it for them
        $host_option = 'host=;server='.$connection_options['host'];
        if (!empty($connection_options['port']) && !strpos($host_option, ',') !== false)
          $host_option .= ','.$connection_options['port'];
        $options[] = $host_option;
      }
    }

    $options[] = 'database='.$connection_options['database'];

    if (isset($connection_options['odbc_driver'])) {
      $options[] = 'driver='.$connection_options['odbc_driver'];
      if ($connection_options['odbc_driver'] == 'FreeTDS' && isset($connection_options['tds_version'])) {
        $options[] = 'TDS_Version='.$connection_options['tds_version'];
      }
    }

    // Launch the connection to the server.
    $dsn = 'odbc:'.implode(';', $options);
    $this->log('Connection string: %dsn', array('%dsn' => $dsn), WATCHDOG_INFO);

    parent::__construct($dsn, $connection_options['username'], $connection_options['password'], array(
      PDO::ATTR_EMULATE_PREPARES => true,
    ));

    $this->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

    // Fetch the name of the user-bound schema. It is the schema that SQL Server
    // will use for non-qualified tables.
    $this->log('DatabaseStatement_odbc: is this log working?', null, WATCHDOG_DEBUG);
    $this->schema()->defaultSchema = $this->query("SELECT SCHEMA_NAME()")->fetchField();

    // If running under Drush, fiddle with the connection settings to make sure
    //  `drush_valid_db_credentials` builds the right query string.
    // (but first, make sure it really is the default db)
    if (drupal_is_cli()) {
      $conn = &$GLOBALS['databases']['default']['default'];
      if (isset($conn) && isset($conn['driver']) && $conn['driver'] == 'odbc' && isset($conn['odbc_driver']) && !preg_match('/;driver=/', $conn['database'])) {
        $driver = $conn['odbc_driver'];
        $server = $conn['host'] . (!empty($conn['port']) ? ',' . $conn['port'] : '');
        $conn['database'] = $conn['database'].';driver='.$driver.';server='.$server;
        echo "odbc-sqlsrv: Adjusted connection settings\n";
      }
    }

    $this->log('DatabaseStatement_odbc: init', null, WATCHDOG_DEBUG);
  }

  /**
   * Clean up faux temporary tables
   */
  public function __destruct() {
    $tables = $this->temporaryTables;
    $tables = array_values(array_unique(array_filter($tables)));

    foreach ($tables as $table) {
      $this->query('DROP TABLE '.$table);
    }
  }

  /**
   * Override of PDO::prepare(): prepare a prefetching database statement.
   *
   * @status tested
   */
  public function prepare($query, $options = array()) {
    $query = $this->preprocessQuery($query);
    return new DatabaseStatement_odbc($this, $query, $options);
  }

  /**
   * Temporary override of DatabaseConnection::prepareQuery().
   *
   * @todo: remove that when DatabaseConnection::prepareQuery() is fixed to call
   *   $this->prepare() and not parent::prepare().
   * @status: tested, temporary
   */
  public function prepareQuery($query) {
    $query = $this->prefixTables($query);

    // Call our overriden prepare.
    return $this->prepare($query);
  }

  public function exec($query) {
    if (!$this->bypassQueryPreprocess) {
      $query = $this->preprocessQuery($query);
    }

    // $query = $this->prefixTables($query);

    return parent::exec($query);
  }

  /**
   * Internal function: prepare a query by calling PDO directly.
   *
   * This function has to be public because it is called by other parts of the
   * database layer, but do not call it directly, as you risk locking down the
   * PHP process.
   */
  public function PDOPrepare($query, array $options = array()) {
    if (!$this->bypassQueryPreprocess) {
      $query = $this->preprocessQuery($query);
    }

    $this->log('PDOPrepare: %query', array('%query' => $query), WATCHDOG_INFO);
    return parent::prepare($query, $options);
  }

  public function PDOExec($query, array $options = array()) {
    if (!$this->bypassQueryPreprocess) {
      $query = $this->preprocessQuery($query);
    }

    $this->log('PDOExec: %query', array('%query' => $query), WATCHDOG_INFO);
    return parent::exec($query);
  }

  /**
   * This is the original replacement regexp from Microsoft.
   *
   * We could probably simplify it a lot because queries only contain
   * placeholders when we modify them.
   *
   * NOTE: removed 'escape' from the list, because it explodes
   * with LIKE xxx ESCAPE yyy syntax.
   */
  const RESERVED_REGEXP = '/\G
    # Everything that follows a boundary that is not : or _.
    \b(?<![:\[_])(?:
      # Any reserved words, followed by a boundary that is not an opening parenthesis.
      (action|admin|alias|any|are|array|at|begin|boolean|class|commit|contains|current|data|date|day|depth|domain|external|file|full|function|get|go|host|input|language|last|less|local|map|min|module|new|no|object|old|open|operation|parameter|parameters|path|plan|prefix|proc|public|ref|result|returns|role|row|rows|rule|save|search|second|section|session|size|state|statistics|temporary|than|time|timestamp|tran|translate|translation|trim|user|value|variable|view|without)
      (?!\()
      |
      # Or a normal word.
      ([a-z]+)
    )\b
    |
    \b(
      [^a-z\'"\\\\]+
    )\b
    |
    (?=[\'"])
    (
      "  [^\\\\"] * (?: \\\\. [^\\\\"] *) * "
      |
      \' [^\\\\\']* (?: \\\\. [^\\\\\']*) * \'
    )
  /Six';

  protected function replaceReservedCallback($matches) {
    if ($matches[1] !== '') {
      // Replace reserved words.
      return '[' . $matches[1] . ']';
    }
    else {
      // Let other value passthru.
      reset($matches);
      foreach ($matches as $i => $match) {
        if ($i < 2) continue;
        if ($match !== '') return $match;
      }
    }
    return '';
  }

  public function quote($str, $paramType = PDO::PARAM_STR) {
    $hasNewlines = false;

    if (is_null($str)) return 'NULL';
    if (is_array($str)) {
      $this->log('Attempt to use array as string parameter: %arr', array('%arr' => $str), WATCHDOG_NOTICE);
      return "''";
    }
    if (is_object($str)) {
      $this->log('Attempt to use object as string parameter: %arr', array('%arr' => $str), WATCHDOG_NOTICE);
      return "''";
    }

    $quoted = $str;
    if (ODBC_LOGGING_DEBUG) echo "Quoting string: original = [[ $quoted ]]\n";
    if (ODBC_HEX_DEBUG) { echo "Quoting string: original (md5: ".md5($quoted).")\n"; $this->hex_dump($quoted); }
    $quoted = $this->encode($quoted);
    if (ODBC_LOGGING_DEBUG) echo "Quoting string: encoded  = [[ $quoted ]]\n";
    if (ODBC_HEX_DEBUG) { echo "Quoting string: encoded (md5: ".md5($quoted).")\n"; $this->hex_dump($quoted); }

    $quoted = str_replace("'", "''", $quoted);
    $quoted = "'".$quoted."'";
    if (ODBC_LOGGING_DEBUG) echo "Quoting string: final    = [[ $quoted ]]\n";
    if (ODBC_HEX_DEBUG) { echo "Quoting string: final (md5: ".md5($quoted).")\n"; $this->hex_dump($quoted); }
    return $quoted;
  }

  public function encode($str) {
    $fn = $this->encode;
    return $fn($str);
  }

  public function decode($str) {
    $fn = $this->decode;
    return $fn($str);
  }

  public function quoteIdentifier($identifier) {
    return '[' . $identifier .']';
  }

  public function escapeField($field) {
    if (strlen($field) > 0) {
      $fieldname = preg_replace('/[^A-Za-z0-9_.]+/', '', $field);
      $field_parts = explode('.', $fieldname);
      $out = '';
      reset($field_parts);
      foreach ($field_parts as $part) {
        if (!empty($out))
          $out .= '.';
        $out .= $this->quoteIdentifier($part);
      }
      return $out;
    }
    else {
      return '';
    }
  }

  public function quoteIdentifiers($identifiers) {
    $out = array();
    reset($identifiers);
    foreach ($identifiers as $ident) {
      $out[]= $this->quoteIdentifier($ident);
    }
    return $out;
  }

  /**
   * Override of DatabaseConnection::query().
   */
  public function query($query, array $args = array(), $options = array()) {
    $this->log('DatabaseConnection_odbc: query: %query', array('%query' => $query), WATCHDOG_DEBUG);
    try {
      return parent::query($query, $args, $options);
    } catch (Exception $e) {
      if (ODBC_LOGGING_DEBUG) echo "Exception in DatabaseConnection_odbc.query()\n";
      $this->log('DatabaseConnection_odbc: query: '.get_class($e).' when executing "%query" with variables %vars: '.$e->getMessage(), array('%query' => $query, '%vars' => $args), WATCHDOG_ERROR);
      throw $e;
    }
  }

  /**
   * Override of DatabaseConnection::queryRange().
   */
  public function queryRange($query, $from, $count, array $args = array(), array $options = array()) {
    $query = $this->addRangeToQuery($query, $from, $count);
    return $this->query($query, $args, $options);
  }

  /**
   * Override of DatabaseConnection::queryTemporary().
   *
   * @status tested
   */
  public function queryTemporary($query, array $args = array(), array $options = array()) {
    // Generate a new temporary table name and protect it from prefixing.
    // SQL Server requires that temporary tables to be non-qualified.
    if ($this->fauxTemporaryTables) {
      $tablename = $this->generateTemporaryTableName();
      $this->temporaryTables[] = $tablename;
    } else {
      $tablename = '##'.$this->generateTemporaryTableName();
    }

    $prefixes = $this->prefixes;
    $prefixes[$tablename] = '';
    $this->setPrefix($prefixes);

    // Replace SELECT xxx FROM table by SELECT xxx INTO #table FROM table.
    $query = preg_replace('/^SELECT(.*?)FROM/is', 'SELECT$1 INTO ' . $tablename . ' FROM', $query);

    $this->query($query, $args, $options);
    return $tablename;
  }

  /**
   * Generates a temporary table name.
   *
   * @return
   *   A table name.
   */
  protected function generateTemporaryTableName() {
    return "db_temporary_".$this->requestId().'_'.$this->temporaryNameIndex++;
  }

  public function requestId() {
    global $odbc_request_id, $bang_syslog__id;
    if (!isset($odbc_request_id)) {
      if (isset($bang_syslog__id))
        $odbc_request_id = $bang_syslog__id.'_'.substr(md5(rand()), 6, 6);
      else
        $odbc_request_id = substr(md5(rand()), 6, 6);
    }
    return $odbc_request_id;
  }


  /**
   * Internal function: do a manual placeholder replacement for a query.
   * New version to build into a buffer rather than using str_replace, to avoid falling foul of copies of placeholders in error messages
   */
  public function replacePlaceholders($query, array $args = array(), $inline_values = true, &$replaced_keys = null) {
    if (empty($args))
      return $query;

    // make sure the values are all formatted for SQL
    if ($inline_values) {
      $args = $this->inlineValues($args);
      // $args = array_map(array($this, 'inlineValue'), $args);
      if (ODBC_LOGGING_DEBUG) echo('replacePlaceholders: values = [[ '.print_r($args, true)." ]]\n");
    }
    // $this->log('Placeholders in: '.$query, null, WATCHDOG_DEBUG);
    // $this->log('Placeholders: '.json_encode(array_keys($args)));


    //  locate the arguments in the input string
    $placeholders = array();
    $known_keys = array();
    if (range(0, count($args) - 1) === array_keys($args)) {
      //  using ordered arguments,  ?
      //  we can be sure the arguments are in order
      $pos = 0;
      $offset = 0;
      $currentArg = 0;
      $c_args = count($args);
      for ($pos = strpos($query, '?'); $pos !== false && $currentArg < $c_args; $pos = strpos($query, '?', $offset)) {
        $placeholders[] = array($pos, '?', $args[$currentArg]);
        $currentArg++;
        $offset = $pos + 1;
      }
    } else {
      //  using named arguments,  :name
      //  these aren't necessarily in order in SQL, so get positions for all the ones we can find
      reset($args);
      foreach ($args as $key => $value) {
        if ($key[0] != ':' && $key[0] != '%') {
          $key = ":$key";
        }

        $pos = 0;
        $offset = 0;
        for ($pos = strpos($query, $key); $pos !== false ; $pos = strpos($query, $key, $offset)) {
          $known_keys[] = $key;
          if (substr($query, $pos, strlen($key)) != $key) $this->log("Mismatching placeholder at offset $index: \"".substr($query, $pos, strlen($key))."\" != \"$key\"", null, WATCHDOG_ERROR);
          $placeholders[$pos] = array($pos, $key, $value);
          $offset = $pos + strlen($key);
        }
      }
      // sort the indices by their position
      ksort($placeholders);
      $placeholders = array_values($placeholders);
    }

    if (ODBC_LOGGING_DEBUG) echo('replacePlaceholder: locations = '.print_r($placeholders, true)."\n");

    // build the output buffer
    $buf = "";
    $offset = 0;
    reset($placeholders);
    foreach ($placeholders as $placeholder) {
      list($index, $key, $value) = $placeholder;
      if (is_object($value) || is_array($value)) { $this->log('Skipping obj/array argument: '.print_r($value, true), null, WATCHDOG_NOTICE); continue; }
      // if (substr($query, $index, strlen($key)) != $key) $this->log("Mismatching placeholder at offset $index: \"".substr($query, $index, strlen($key))."\" != \"$key\"", null, WATCHDOG_ERROR);
      $buf .= substr($query, $offset, $index - $offset);
      $buf .= $value;
      $offset = $index + strlen($key);
    }
    $buf .= substr($query, $offset);

    if (!is_null($replaced_keys)) {
      $replaced_keys = array_unique($known_keys);
    }

    return $buf;
  }

  function inlineValue($value) {
    // if (is_array($value) && count($value) == 1 && isset($value[0]))
    //   $value = $value[0];
    if (is_array($value)) {
      $values = $this->inlineValues($value);
      // $values = array_map(array($this, 'inlineValue'), $value);
      return implode(', ', $values);
    }
    if (is_string($value))
      return $this->quote($value);
    if (is_null($value))
      return 'null';
    if (is_float($value))
      return sprintf('%F', $value);
    if (is_int($value))
      return sprintf('%d', $value);
    return $this->quote($value);
  }

  function inlineValues($values) {
    $out = array();
    reset($values);
    foreach ($values as $key => $value) {
      $out[$key] = $this->inlineValue($value);
    }
    return $out;
  }

  /**
   * Internal function: massage a query to make it compliant with SQL Server.
   */
  public function preprocessQuery($query) {
    // Force quotes around some SQL Server reserved keywords.
    if (preg_match('/^SELECT/', $query)) {
      $query = preg_replace_callback(self::RESERVED_REGEXP, array($this, 'replaceReservedCallback'), $query);
    }

    // Last chance to modify some SQL Server-specific syntax.
    $replacements = array(
      // Normalize SAVEPOINT syntax to the SQL Server one.
      '/^SAVEPOINT (.*)$/' => 'SAVE TRANSACTION $1',
      '/^ROLLBACK TO SAVEPOINT (.*)$/' => 'ROLLBACK TRANSACTION $1',
      // SQL Server doesn't need an explicit RELEASE SAVEPOINT.
      // Run a non-operaiton query to avoid a fatal error
      // when no query is runned.
      '/^RELEASE SAVEPOINT (.*)$/' => 'SELECT 1 /* $0 */',
    );
    $query = preg_replace(array_keys($replacements), $replacements, $query);

    // Add prefixes to Drupal-specific functions.
    $functions = array(
      // 'SUBSTRING',
      'SUBSTRING_INDEX',
      'GREATEST',
      'CONCAT',
      'IF',
      'UNIX_TIMESTAMP',
    );
    $schema = $this->schema()->defaultSchema;
    reset($functions);
    foreach ($functions as $function) {
      $query = preg_replace('/\b(?<![:.])(' . $function . ')\(/i', $schema . '.$1(', $query);
    }

    $replacements = array(
      'LENGTH' => 'LEN',
      'POW' => 'POWER',
      'NOW' => 'GETUTCDATE',
    );
    reset($replacements);
    foreach ($replacements as $function => $replacement) {
      $query = preg_replace('/\b(?<![:.])(' . $function . ')\(/i', $replacement . '(', $query);
    }

    // Replace the ANSI concatenation operator with SQL Server poor one.
    $query = preg_replace('/\|\|/', '+', $query);

    return $query;
  }

  /**
   * Internal function: massage aliased expressions to accomodate edge cases that don't quite work the same on SQL Server.
   */
  public function preprocessExpressions (&$expressionFields, &$groupBy = null) {
    if (!is_array($expressionFields))
      return;

    $replace_expressions = array();
    foreach ($expressionFields as $field => $expr) {
      if ($new_expression = $this->preprocessExpression($expr['expression'])) {
        $replace_expressions[$expr['expression']] = $new_expression;
        $expr['expression'] = $new_expression;
        $expressionFields[$field] = $expr;
      }
    }

    if (!is_null($groupBy)) {
      foreach ($groupBy as $i => $grp) {
        if (isset($expressionFields[$grp])) {
          $groupBy[$i] = $expressionFields[$grp]['expression'];
        } else if (!empty($replace_expressions[$grp])) {
          $groupBy[$i] = $replace_expressions[$grp];
        } else if ($new_expression = $this->preprocessExpression($grp)) {
          $groupBy[$i] = $new_expression;
        }
      }
    }
  }

  private function preprocessExpression($expression) {
    if (is_string($expression)) {
      if (preg_match('/.* LIKE .*/i', $expression)) {
        if (!preg_match('/^\case when /', $expression)) {
          return 'case when '.$expression.' then 1 else 0 end';
        }
      }

      if (preg_match('/CONCAT\((.*)\)/', $expression, $match)) {
        $this->log('Preprocessing CONCAT', $match);
        $parts = split(',', $match[1]);
        $parts = array_map('trim', $parts);
        // TODO: check quote balance and coalesce unbalanced parts
        if (count($parts) > 2) {
          $alt = $this->preprocessExpression__concat_parts($parts);
          return $alt;
        }
      }
    }

    return false;
  }

  private function preprocessExpression__concat_parts($parts) {
    if (count($parts) == 2) {
      return 'CONCAT('.$parts[0].', '.$parts[1].')';
    }
    $firstPart = array_shift($parts);
    return 'CONCAT('.$firstPart.', '.$this->preprocessExpression__concat_parts($parts).')';
  }


  /**
   * Internal function: add range options to a query.
   *
   * This cannot be set protected because it is used in other parts of the
   * database engine.
   *
   * @status tested
   */
  public function addRangeToQuery($query, $from, $count) {
    if (preg_match('/TOP\(/', $query)) {
      return $query;
    }

    if ($from == 0) {
      // Easy case: just use a TOP query if we don't have to skip any rows.
      $query = preg_replace('/^\s*SELECT(\s*DISTINCT)?/Dsi', 'SELECT$1 TOP(' . $count . ') WITH TIES ', $query);
      if (!preg_match('/ORDER BY/', $query))
        $query = $query." ORDER BY @@VERSION ASC";
    }
    else {
      // More complex case: use a TOP query to retrieve $from + $count rows, and
      // filter out the first $from rows using a window function.
      $query = preg_replace('/^\s*SELECT(\s*DISTINCT)?/Dsi', 'SELECT$1 TOP(' . ($from + $count) . ') WITH TIES ', $query);
      $query = '
        SELECT * FROM (
          SELECT sub2.*, ROW_NUMBER() OVER(ORDER BY sub2.__line2) AS __line3 FROM (
            SELECT 1 AS __line2, sub1.* FROM (' . $query . ') AS sub1
          ) as sub2
        ) AS sub3
        WHERE __line3 BETWEEN ' . ($from + 1) . ' AND ' . ($from + $count);
      if (!preg_match('/ORDER BY/', $query))
        $query = $query." ORDER BY @@VERSION ASC";
    }

    return $query;
  }

  public function mapConditionOperator($operator) {
    // SQL Server doesn't need special escaping for the \ character in a string
    // literal, because it uses '' to escape the single quote, not \'. Sadly
    // PDO doesn't know that and interpret \' as an escaping character. We
    // use a function call here to be safe.
    static $specials = array(
      'LIKE' => array('postfix' => " ESCAPE CHAR(92)"),
      'NOT LIKE' => array('postfix' => " ESCAPE CHAR(92)"),
    );
    return isset($specials[$operator]) ? $specials[$operator] : null;
  }

  /**
   * Override of DatabaseConnection::nextId().
   *
   * @status tested
   */
  public function nextId($existing = 0) {
    // If an exiting value is passed, for its insertion into the sequence table.
    if ($existing > 0) {
      try {
        $this->query('SET IDENTITY_INSERT {sequences} ON; INSERT INTO {sequences} (value) VALUES(:existing); SET IDENTITY_INSERT {sequences} OFF', array(':existing' => $existing));
      }
      catch (Exception $e) {
        if (ODBC_LOGGING_DEBUG) echo "Exception in DatabaseConnection_odbc.nextId()\n";
        // Doesn't matter if this fails, it just means that this value is already
        // present in the table.
      }
    }

    return $this->query('INSERT INTO {sequences} DEFAULT VALUES', array(), array('return' => Database::RETURN_INSERT_ID));
  }

  public function lastInsertId($name = null) {
    $last = $this->query('SELECT @@IDENTITY')->fetchField();
    $this->log('Last insert ID: %id', array('%id' => $last), WATCHDOG_INFO);
    return $last;
  }

  /**
   * Override DatabaseConnection::escapeTable().
   *
   * @status needswork
   */
  public function escapeTable($table) {
    // Rescue the # prefix from the escaping.
    return ($table[0] == '#' ? ($table[1] == '#' ? '##' : '#') : '') . preg_replace('/[^A-Za-z0-9_.]+/', '', $table);
  }
}

/**
 * @} End of "ingroup database".
 */

/**
 * Workaround a bug in the database autoloader by defining all the child classes.
 *
 * @see http://drupal.org/node/851136
 */
class DatabaseTransaction_odbc extends DatabaseTransaction {

}

