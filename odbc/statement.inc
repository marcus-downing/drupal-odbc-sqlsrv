<?php


class DatabaseStatement_odbc extends DatabaseStatementPrefetch implements Iterator, DatabaseStatementInterface {
	private $debug = false;

	public $range = null;

	// public $intent;

	// public function intent() {
	// 	if (empty($this->intent)) {
	// 		$query = $this->queryString;
	// 		if ($query)
	// 	}
	// }

	private function log($message, $variables = null, $severity = WATCHDOG_NOTICE) {
		if (is_null($variables))
			$variables = array();
		$variables['this'] = $this;
		if ($this->debug)
			$variables['threshold'] = WATCHDOG_DEBUG;
		$this->dbh->log('DatabaseStatement_odbc: '.$message, $variables, $severity);
	}

	public function debug() {
		$this->debug = true;
		$this->log('Enabling debug');
		return $this;
	}

	private function _getStatement($query) {
		$this->log('getStatement(%query)', array('%query' => $query), WATCHDOG_DEBUG);
		return $this->dbh->PDOPrepare($query);
	}


	public function fetch($fetch_style = null, $cursor_orientation = PDO::FETCH_ORI_NEXT, $cursor_offset = null) {
		$this->log('fetch(%style, %orientation)', array('%style' => $fetch_style, '%orientation' => $cursor_orientation), WATCHDOG_DEBUG);
		return parent::fetch($fetch_style, $cursor_orientation, $cursor_offset);
	}

	public function fetchAll($fetch_style = null, $fetch_column = null, $constructor_args = null) {
		$this->log('fetch(%style, %column)', array('%style' => $fetch_style, '%column' => $fetch_column), WATCHDOG_DEBUG);
		return parent::fetchAll($fetch_style, $fetch_column, $constructor_args);
	}

	public function fetchColumn($index = 0) {
		$this->log('fetchColumn(%index)', array('%index' => $index), WATCHDOG_DEBUG);
		return parent::fetchColumn($index);
	}

	public function fetchObject($class_name = null, $constructor_args = array()) {
		$this->log('fetchObject(%class)', array('%class' => $class_name), WATCHDOG_DEBUG);
		return parent::fetchObject($class_name, $constructor_args);
	}

	public function rowCount() {
		$nrows = parent::rowCount();
		$this->log('rowCount() = %nrows', array('%nrows' => $nrows), WATCHDOG_DEBUG);
		return $nrows;
	}

	private function _setFetchMode($options) {
		if (isset($options['fetch'])) {
			if (is_string($options['fetch'])) {
				// Default to an object. Note: db fields will be added to the object
				// before the constructor is run. If you need to assign fields after
				// the constructor is run, see http://drupal.org/node/315092.
				$this->setFetchMode(PDO::FETCH_CLASS, $options['fetch']);
			}
			else {
				$this->setFetchMode($options['fetch']);
			}
		}
	}

	private function _prepareQueryString(&$args) {
		$queryString = $this->dbh->prefixTables($this->queryString);
		$queryString = $this->dbh->replacePlaceholders($queryString, $args);

		if (empty($args))
			$this->log('execute: Preparing query: %query', array('%query' => $this->queryString), WATCHDOG_DEBUG);
		else
			$this->log('execute: Preparing query: %query with args: %args', array('%query' => $this->queryString, '%args' => $args), WATCHDOG_DEBUG);

		return $queryString;
	}

	private function _fetchResults(&$statement, $options) {
		$this->rowCount = $statement->rowCount();

		if ($this->isSelect()) {
			try {
				$this->data = $statement->fetchAll(PDO::FETCH_ASSOC);
				// $this->log('execute: Fetched %nrows rows', array('%nrows' => count($this->data)), WATCHDOG_DEBUG);

				//  skip records the dumb way
				if (!empty($options['range'])) {
					$offset = is_numeric($options['range']['start']) ? intval($options['range']['start']) : 0;
					$limit = is_numeric($options['range']['length']) ? intval($options['range']['length']) : null;
					if ($offset > 0 || !is_null($limit)) {
						// bang_syslog('Slicing data: offset %s, len %s of %s', $offset, $limit, count($this->data));
						$this->data = array_slice($this->data, $offset, $limit);
					}
				}

				//  decode strings if necessary
				$this->decodeData();
			}
			catch (Exception $e) {
				if (ODBC_LOGGING_DEBUG) echo "Exception in DatabaseStatement_odbc.execute()\n";
				$this->log(get_class($e).' when fetching results for "%sql": '.$e->getMessage(), array('%sql' => $this->queryString), WATCHDOG_ERROR);
				// debug_print_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS);
				// tolerate this error and return nothing
				$this->data = array();
			}

			$this->inferColumnNames();

			if (isset($options['sqlsrv_drop_columns']))
				$this->dropColumns($options['sqlsrv_drop_columns']);
		}
	}

	private function _execute($queryString, $options) {
		$statement = $this->_getStatement($queryString);

		if ($statement === false) {
			$this->log('execute: No statement for query "%sql"', array('%sql' => $this->queryString), WATCHDOG_ERROR);
			$this->throwPDOException();
		}
		$return = (bool) $statement->execute();
		if ($return === false) {
			$this->log('execute: Failed to execute query "%sql"', array('%sql' => $this->queryString), WATCHDOG_ERROR);
			$this->throwPDOException();
		}

		// Fetch all the data from the reply, in order to release any lock as soon as possible.
		$this->_fetchResults($statement, $options);
		$statement->closeCursor();

		// Initialize the first row in $this->currentRow.
		$this->next();
		return $return;
	}

	public function execute($args = array(), $options = array()) {
		// $this->log('execute()', null, WATCHDOG_DEBUG);

		$this->_setFetchMode($options);
		$queryString = $this->_prepareQueryString($args);
		
		$return = $this->_execute($queryString, $options);
		// unset($statement);

		return $return;
	}

	function isSelect() {
		return preg_match('/^SELECT/', $this->queryString);
	}

	function inferColumnNames() {
		if (count($this->data)) {
			$this->columnNames = array_keys($this->data[0]);
		} else {
			$this->columnNames = array();
		}
	}

	/**
	 * Remove technical columns from the final result set.
	 * TODO: Do we want to rename these options?
	 */
	function dropColumns($columns) {
		$droppable_columns = array_flip($columns);
		$dropped_columns = array();
		foreach ($this->columnNames as $k => $column) {
			if (substr($column, 0, 2) == '__' || isset($droppable_columns[$column])) {
				$dropped_columns[] = $column;
				unset($this->columnNames[$k]);
			}
		}

		if ($dropped_columns) {
			// Renumber columns.
			$this->columnNames = array_values($this->columnNames);

			foreach ($this->data as $k => $row) {
				foreach ($dropped_columns as $column) {
					unset($this->data[$k][$column]);
				}
			}
		}
	}

	public function decodeData() {
		if ($this->dbh->encoding == '')
			return;
		if (!is_array($this->data))
			return;
		if (count($this->data) > 0 && isset($this->data[0]) && !is_array($this->data[0]))
			return;

		// $this->data = array_map(function ($row) {
		// 	return array_map(function ($value) {
		// 		return unicode_x_decode($value);
		// 	}, $row);
		// }, $this->data);

		$decoded_rows = array();
		$rows = $this->data;
		reset($rows);
		foreach ($rows as $row) {
			$decoded_row = array();
			foreach ($row as $key => $value) {
				$decoded_value = unicode_x_decode($value);
				$decoded_row[$key] = $decoded_value;
			}
			$decoded_rows[] = $decoded_row;
		}

		// if ($decoded_rows !== $this->data)
		// 	$this->log('Decoded %before -> %after', array('%before' => $this->data, '%after' => $decoded_rows), WATCHDOG_DEBUG);

		$this->data = $decoded_rows;
	}
}