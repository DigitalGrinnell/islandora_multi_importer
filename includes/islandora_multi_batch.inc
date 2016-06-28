<?php

/**
 * @file
 * A batch ingest implement, (preprocessor and object) for twig template based Multi importer.
 *
 * Works similar to other importers derived from islandora_importer module.
 */

/**
 * A class which is capable of using Twig templates and tabulated row data to build an object.
 *
 */
class IslandoraMultiBatch extends IslandoraBatchPreprocessor {

  // Change to FALSE if one wants to take control over hierarchical structures.
  // @todo Make zip scan respect this.
  public $recursiveScan = TRUE;
  protected $itemClass = "IslandoraMultiBatchObject";
  protected static $internal_pidcache = array();//We need PIDS generated on getingestinfo
  
  /**
   *  Static function to get PIDs. 
   *
   * @param string $namespace
   *    A valid fedora pid namespace
   *
   * @param int $cachesize
   *    Size of the temp cache for identifiers for this
   *    namespace.  
   *
   * @return string 
   *   a PID for this namespace.
   */
  protected function getInternalIdentifier($namespace, $cachesize) {
    module_load_include('inc', 'islandora', 'includes/utilities');
    if (empty(self::$internal_pidcache[$namespace])) {
      // Get enough PIDs given an initial $cachesize
      // (plus one, so we'll always get at least one).
      //Instead of using the build in parent function use the Islandora API to get the identifier using de uuid variable
      self::$internal_pidcache[$namespace] = (array) $this->connection->repository->getNextIdentifier($namespace, variable_get('islandora_basic_collection_generate_uuid', FALSE), $cachesize + 1);
    }

    return array_shift(self::$internal_pidcache[$namespace]);
  }

  /**
   * Try to get the namespace from the collection policy.
   *
   * @param string
   *    A PID
   *
   * @return string|NULL
   *   The namespace with which the object should be ingested.
   *   NULL if we use the Fedora configured default.
   */
  protected function determineNamespacebyPID($parent_pid, $cmodel) {
    // Get namespace based on collection.
    $collection_policies =& drupal_static(__METHOD__, array());
    // Parameter not set.
    $namespace = NULL;
    if (module_exists('islandora_basic_collection') && !isset($collection_policies[$parent_pid])) {
      $parent = islandora_object_load($parent_pid);
      if (isset($parent['COLLECTION_POLICY'])) {
        $policy_stream = $parent['COLLECTION_POLICY'];
        $collection_policies[$parent_pid] = new CollectionPolicy($policy_stream->content);
      }
      else {
        // If not collection, just return the namespace of the 
        // Parent PID, assuming they will inherit, like pages, etc?
        // @TODO maybe we need a recursive namespace thing until we hit a collection?
        $namespace_tmp =  explode(':', $parent_pid);
        $namespace = trim($namespace_tmp[0]);
      }
    }
    if (isset($collection_policies[$parent_pid])) {
      $info = $collection_policies[$parent_pid]->getContentModels();
      $applicable = array_intersect_key($info, drupal_map_assoc(array($cmodel)));
      if ($applicable) {
        $first_applicable = reset($applicable);
        $namespace = $first_applicable['namespace'];
      }
      else {
        $namespace = "islandora";
      }
    }
    return $namespace;
  }
  /**
   * Try to get the cmodels from the parent objet.
   *
   * @param string
   *    A PID
   *
   * @return string|NULL
   *   The namespace with which the object should be ingested.
   *   NULL if we use the Fedora configured default.
   */
  protected function getParentCmodel($parent_pid) {
    // Get namespace based on collection.
    $cmodels =& drupal_static(__METHOD__, array());
    // Parameter not set.
    $current_cmodel = NULL;
    $parent = islandora_object_load($parent_pid);
    if ($parent) {
      $cmodels[$parent_pid] = $parent->models;
    }
    else {
      $cmodels[$parent_pid] = FALSE; // Means the object does not exist. So error
    }
    
    if (isset($cmodels[$parent_pid])) {
      $current_cmodel = $cmodels[$parent_pid];
    }
    return $current_cmodel;
  }
  /**
   * Get a listing of "file-object"-like entries.
   *
   * @return array
   *   An associative array of stdClass objects representing files. Array keys
   *   are URIs relative to the "target", and the objects properties include:
   *   - uri: A string containing the complete URI of the resource.
   *   - filename: The filename.
   *   - name: The filename without its extension.
   */
  protected function scan() {
    $method = "scan" . ucfirst($this->getTargetType());
    return $this->$method($this->getTarget());
  }

  /**
   * Get the target resource.
   */
  protected function getTarget() {
    return $this->parameters['target'];
  }

  /**
   * Get the type of the target resource.
   *
   * Prefixed with "scan_" to determine which method gets called to generate
   * the list of resource.
   */
  protected function getTargetType() {
    return $this->parameters['type'];
  }

  /**
   * Allow the pattern to be set differently.
   */
  protected static function getPattern() {
    return '/.*/';
  }

  /**
   * Scan the directory with file_scan_directory().
   */
  protected function scanDirectory($target) {
    $files = array();
    $target_path = drupal_realpath($target);
    $target_length = strlen($target_path) + 1;
    $directory_contents = file_scan_directory(
      $target_path,
      $this->getPattern(),
      array('recurse' => $this->recursiveScan)
    );
    foreach ($directory_contents as $uri => $value) {
      $files[substr($uri, $target_length)] = $value;
    }
    return $files;
  }

  /**
   * Generate output analogous to file_scan_directory().
   */
  protected function scanZip($target) {
    $zip = new ZipArchive();

    $zip_file = drupal_realpath($target);
    if (($error = $zip->open($zip_file)) !== TRUE) {
      return FALSE;
    }

    $count = $zip->numFiles;

    $info = array();
    for ($i = 0; $i < $count; $i++) {
      $raw_name = $zip->getNameIndex($i, ZipArchive::FL_UNCHANGED);
      if (isset($this->parameters['zip_encoding']) && trim($this->parameters['zip_encoding']) !== '') {
        $name = drupal_convert_to_utf8($raw_name, $this->parameters['zip_encoding']);
      }
      else {
        $name = $zip->getNameIndex($i);
      }
      $path_info = pathinfo($name);

      // Avoid creating entries for directories and some weird Mac stuff...
      // XXX: Should be made to take the pattern from
      // $this::getPattern(), and to filter the results based on it.
      if (isset($path_info['extension']) && strpos($path_info['dirname'], '__MACOSX') === FALSE && $path_info['extension'] != 'DS_Store') {
        $file_entry = new stdClass();
        $file_entry->uri = "zip://$zip_file#$raw_name";
        $file_entry->filename = $path_info['basename'];
        $file_entry->name = $path_info['filename'];
        $info[$name] = $file_entry;
      }
    }

    $zip->close();

    return $info;
  }

  /**
   * Group file entries logically, to pass off to the import object.
   *
   * Default implementation groups on files' basename.
   *
   * @param array $files
   *   An array, as returned by file_scan_directory().
   */
  protected function groupFiles($files) {
    $grouped = array();

    // TODO: Group according to basename, to and hand-off each group to object
    // wrappers.
    foreach ($files as $uri => &$file_object) {
      $file_object->ext = pathinfo($file_object->filename, PATHINFO_EXTENSION);
      $grouped[$file_object->name][$file_object->ext] = $file_object;
    }

    return $grouped;
  }

  /**
   * Get the name of the class to instantiate for the batch operations.
   */
  protected static function getObjectClass() {
    return "IslandoraMultiBatchObject";
  }

  /**
   * Perform preprocessing of the scanned resources.
   */
  public function preprocess() {
    //$files = $this->scan();

    //if ($files === FALSE) {
      // Failed to scan.
      // TODO: Pop an error.
   
    //elseif (empty($files)) {
      // No contents.
      // TODO: Pop a warning.
    

    $added = array();
    $object_class = static::getObjectClass();
    $info = $this->getIngestInfo();
    foreach ($info as $index => $object_info) {
      $ingest_object = new $object_class($this->connection, 'New Object via Multi importer', $object_info, $this->parameters);
      $ingest_object->id = $object_info['pid'];
      try {
        $ingest_object->addRelationships();
        $this->addToDatabase($ingest_object, $ingest_object->getResources(),$object_info['parent']);
        $added[] = $ingest_object;
      }
      catch (Exception $e) {
        watchdog_exception('islandora_batch', $e);
      }
    }

    return $added;
  }


  /*$parameters = array(
    'type' => drush_get_option('type'),
    'target' => drush_get_option('target'),
    'parent' => drush_get_option('parent', variable_get('islandora_repository_pid', 'islandora:root')),
    'parent_relationship_uri' => drush_get_option('parent_relationship_uri', 'info:fedora/fedora-system:def/relations-external#'),
    'parent_relationship_pred' => drush_get_option('parent_relationship_pred', 'isMemberOfCollection'),
    'namespace' => drush_get_option('namespace'),
    'zip_encoding' => drush_get_option('zip_encoding'),
  );

  if ($content_models = drush_get_option('content_models', FALSE)) {
    $parameters['content_models'] = explode(',', $content_models);
  }
  else {
    $parameters['content_models'] = array();
  }*/
  protected function getIngestInfo() {
    error_log('running getIngestInfo');
    $file = file_load($this->parameters['source_data']);
    $file_path = drupal_realpath($file->uri);
    $file_data_all = islandora_multi_importer_read_filedata($file_path, -1 , $offset = 0);
    $this->parameters['data']['headers'] =  $file_data_all['headers'];
    $namespace_hash = array();
    $parent_hash = array(); // Keeps track of all parents and child that don't have a PID assigned
    $namespace_count = array();
    $info = array();
    $invalid = array(); // Keeps track of invalid rows
    foreach($file_data_all['data'] as $index => $row) {
      //Each row will be an object
     $objectInfo = array();
     $objectInfo['cmodel'] = trim($row[$this->parameters['cmodel_source_field_index']]);
      // Lets start by grouping by parents, namespaces and generate pids
      // namespaces are inherited, so we just need to find collection objects in parent pid column
      $objectInfo['parent'] = trim($row[$this->parameters['object_maping']['parentmap_row']['parentmap']]);
      $objectInfo['data'] = $row;
      if (islandora_is_valid_pid($objectInfo['parent'])) {
        // If valid PID, let's try to fetch a valid namespace for current cmodel
        // we will store all this stuff in a temp hash to avoid hitting this again and again
        $objectInfo['parent_cmodel'] = $this->getParentCmodel($objectInfo['parent']);
        if ($objectInfo['parent_cmodel']) {
          $namespace = isset($namespace_hash[$objectInfo['parent']][$objectInfo['cmodel']])
            ? $namespace_hash[$objectInfo['parent']][$objectInfo['cmodel']] 
            : $namespace_hash[$objectInfo['parent']][$objectInfo['cmodel']] = $this->determineNamespacebyPID($objectInfo['parent'], $objectInfo['cmodel']); 
          //This keeps the count so our pidcache is exact. Nice
          $namespace_count[$namespace]= isset($namespace_hash[$namespace]) ? $namespace_hash[$namespace]++ : 1;
          $objectInfo['namespace'] = $namespace;
        }
        else {
          unset($objectInfo); // No parent cmodel, no object, can't create
          $invalid = $invalid + array($index => $index);
        }
      }
      else {
        // Means our parent object is a ROW index (referencing another row in the spreadsheet)
        // So a different strategy is needed. We will need recurse until we find a non numeric parent.
        $notPID = TRUE;
        $parent = $objectInfo['parent'];
        $parent_hash[$parent][$index] = $index;
        if ((!isset($invalid[$index])) && (!isset($invalid[$parent]))) {
          // Only traverse if we don't have this index or the parent one in the invalid register
          $objectInfo['parent_cmodel'] = $file_data_all['data'][$parent][$this->parameters['cmodel_source_field_index']];
          while($notPID) {
            // Parent rows where shifted by removing the header row
            $parentup = $file_data_all['data'][$parent][trim($row[$this->parameters['object_maping']['parentmap_row']['parentmap']])];
            $parentchilds[$parentup ] = $parentup ;
            if (islandora_is_valid_pid(trim($parentup))) {
              $namespace = isset($pid_hash[$parentup][$objectInfo['cmodel']])
                ? $namespace_hash[$parentup][$objectInfo['cmodel']] 
                : $namespace_hash[$parentup][$objectInfo['cmodel']] = $this->determineNamespacebyPID($parentup, $objectInfo['cmodel']
              ); 
              //This keeps the count so our pidcache is exact. Nice
              $namespace_count[$namespace]= isset($namespace_hash[$namespace]) ? $namespace_hash[$namespace]++ : 1;
              $objectInfo['namespace'] = $namespace; 
              $notPID = false;
            }
            else if(empty(trim($parent))) {
              unset($objectInfo);
              //We can't continue here
              // means there must be an error
              // This will fail for any child object that is child of any of these parents
              $invalid = $invalid + $parentchilds + array($objectInfo['parent'] => $objectInfo['parent']);
              $notPID = false;
            }
            else {
              $parent_hash[$parentup][$parent] = $parent;
            }
            $parent = $parentup;
          }
        } 
        else {
          unset($objectInfo);
        }
      }
      
      if (isset($objectInfo) and !empty($objectInfo)) {
        $info[$index] = $objectInfo;
      }
    }
  // Ok, maybe this is expensive, so let's try it first so, if not 
  // @TODO optimize
  dpm($parent_hash);
  foreach($info as $index => &$objectInfo) {
    
    $namespace = $objectInfo['namespace'];
    dpm($namespace);
    $objectInfo['pid'] = isset($objectInfo['pid']) ? isset($objectInfo['pid']) : $this->getInternalIdentifier($namespace, $namespace_count[$namespace]);
    // Is this object parent of someone?
    if (isset($parent_hash[$objectInfo['parent']])) {
         $objectInfo['parent'] =  $info[$objectInfo['parent']]['pid'];
    }
  }
  dpm($info);
  return $info;
  }

}

class IslandoraMultiBatchObject extends IslandoraBatchObject {
  protected $baseName;
  protected $objectInfo;
  // Keys in objectInfo that are not to be datastreams.
  protected $preprocessorParameters;
  // Path to MODS->DC XSLT.
  public $modsToDcTransform;
  public $deriveDC;
  /**
   * Constructor for the IslandoraScanBatchObject.
   */
  public function __construct(IslandoraTuque $connection, $base_name, $object_info, $preprocessor_parameters) {
    parent::__construct(NULL, $connection->repository);
    //Checks if DC is set a a derivative, if so, we can safely let the derivative process deal with this
    $this->deriveDC = !variable_get('xml_form_builder_use_default_dc_xslts', FALSE);
    $this->modsToDcTransform = drupal_get_path('module', 'islandora_batch') . '/transforms/mods_to_dc.xsl';
    $this->baseName = $base_name;
    $this->objectInfo = $object_info;
    $this->resources = array();
    $this->preprocessorParameters = $preprocessor_parameters;
  }

  /**
   * Function batch_process.
   */
  public function batchProcess() {
    // Use object_info to create some datastreams.
    $this->label = $this->getTitle();
    $errors = array();
    $files = array();
    $datastreams = $this->getDatastreams($errors, $files);
    foreach ($files as $file) {
      $file->status &= ~FILE_STATUS_PERMANENT;
      file_save($file);
      file_usage_add($file, 'islandora_batch', 'islandora_batch_object', $this->getBatchId());
    }
    foreach ($datastreams as $ds) {
      $ds_uri = isset($ds['file']) ?
        $ds['file']->uri :
        $ds['datastream_file'];

      $datastream = $this->constructDatastream($ds['dsid'], $ds['control_group']);
      $datastream->label = $ds['label'];
      $datastream->mimetype = $ds['mimetype'];
      $datastream->setContentFromFile($ds_uri);
      $this->ingestDatastream($datastream);
    }
    return ISLANDORA_BATCH_STATE__DONE;
  }

  /**
   * Acquire base datastreams structure.
   *
   * @see IslandoraImportObject::getDatastreams()
   * Datastreams can be build using multiple sources, so we deal here with that
   */
  protected function getDatastreams(&$errors = NULL, &$files = NULL) {
    module_load_include('inc', 'islandora', 'includes/utilities');
    module_load_include('inc', 'islandora_multi_importer', 'includes/utilities');
    
    $datastreams = array();
    
    if (isset($this->preprocessorParameters['cmodel_mapping'][$this->objectInfo['cmodel']])) { 
      foreach($this->preprocessorParameters['cmodel_mapping'][$this->objectInfo['cmodel']]['dsid']['rows'] as $dsid => $howtogenerate) {
        if (!empty($howtogenerate['method']['data'])) {
          $method = explode("|", $howtogenerate['method']['data']);
          switch($method[0]) {
            case 'derivative':
            //Nothing to do really, derivative processing will do the job
            break;
            case ('xslt' && !$this->deriveDC) :
            // Deal with xslt transforms, we will leave this to the end, since we need the source datastreams to transform
            break;
            case 'template':
            $twig_input = array();
            $template = islandora_multi_importer_twig_fetch($method[1]); 
            $twig_input['name'] = $method[1];
            $twig_input['template'] = $template;
            $twig_input['data'] =  array('data' => array_combine($this->preprocessorParameters['data']['headers'], $this->objectInfo['data']));
            // @TODO should we validate the template again here?
            $output = islandora_multi_importer_twig_process($twig_input);
  					$newfileName="$dsid_".$method[0].'_'.$this->objectInfo['pid'].time().'.xml';
            //$tmp_directory = islandora_multi_importer_temp_directory();
  					$newfile = file_save_data($output,  "public://" . $newfileName);
            // We are assumming the twig template complies with mimetype. @TODO enforce this somehow
            $file[] = $newfile;
            $datastreams[$dsid] = array(
              'dsid' => $dsid,
              'label' => "$dsid datastream",
              'mimetype' => $this->preprocessorParameters['computed_cmodels'][$this->objectInfo['cmodel']][$dsid]['mime'][0],
              'datastream_file' => file_create_url($newfile->uri),
              'filename' => $newfileName,
              'control_group' => 'M',
              //'file' => $file,
            );
            break;
            case 'column':
            // this means our source field contains a file path or and url
            // if file path, we need to know if local of ZIP
            // if URL just fetch the damn thing
            if ($this->preprocessorParameters['type'] == 'ZIP') {
              // Fetch one file from provided ZIP file
              // @TODO Preprocessor should replace the field value with a zip:// path, so just add it
            } 
            else {
              // This will handle all cases, except ZIP
              $fileurl = islandora_multi_importer_remote_file_get(trim($this->objectInfo['data'][$method[1]]));
              if ($fileurl) {
                $datastreams[$dsid] = array(
                  'dsid' => $dsid,
                  'label' => "$dsid datastream",
                  'mimetype' => $this->getMimetype(drupal_basename($fileurl)),
                  'datastream_file' => $fileurl,
                  'filename' => drupal_basename($fileurl),
                  'control_group' => 'M',
                  //'file' => $file,
                );
              }
            }
            break;
            default:
            break;
            
          }; // End Switch
        }
      }
    }
  return $datastreams;
  }  
    
  /**
   * Determine the Mimetype for the given file name.
   *
   * @param string $name
   *   A filename for which to determine a mimetype.
   *
   * @return string 
   * - A string containing the detected mimetype of the file.
   */
  protected function getMimetype($name) {

    static $mime_detect = NULL;
    static $dsids = NULL;

    if ($mime_detect === NULL) {
      $mime_detect = new MimeDetect();
    }

    $mimetype = $mime_detect->getMimetype($name);
    return $mimetype;
  }

  /**
   * Get resources for the current object.
   */
  public function getResources() {
    return $this->resources;
  }

  /**
   * Get the title for this object.
   *
   * Gets the label not empty; otherwise, grabs a title from the MODS.
   */
  protected function getTitle() {
   
      
    return trim($this->objectInfo['data'][$this->preprocessorParameters['object_maping']['labelmap_row']['labelmap']]);
  }

  /**
   * Run an XSLT, and return the results.
   *
   * @param array $info
   *   An associative array of parameters, containing:
   *   - input: The input XML in a string.
   *   - xsl: The path to an XSLT file.
   *   - php_functions: Either a string containing one or an array containing
   *     any number of functions to register with the XSLT processor.
   *
   * @return string
   *   The transformed XML, as a string.
   */
  protected static function runXslTransform($info) {
    $xsl = new DOMDocument();
    $xsl->load($info['xsl']);

    $input = new DOMDocument();
    $input->loadXML($info['input']);

    $processor = new XSLTProcessor();
    $processor->importStylesheet($xsl);

    if (isset($info['php_functions'])) {
      $processor->registerPHPFunctions($info['php_functions']);
    }

    // XXX: Suppressing warnings regarding unregistered prefixes.
    return $processor->transformToXML($input);
  }

  /**
   * Function to get the mods.
   */
  protected function getMods() {
    if (!isset($this['MODS'])) {
      $dir = drupal_get_path('module', 'islandora_batch');
      // TODO: Create the MODS datastream on this object.
      $mods_datastream = $this->constructDatastream('MODS', 'M');
      $mods_datastream->mimetype = 'application/xml';
      $mods_datastream->label = 'MODS Record';

      // If we have an XML stream which is:
      if (isset($this->objectInfo['xml'])) {
        $xml = file_get_contents($this->objectInfo['xml']->uri);

        if ($xml) {
          $s_xml = new SimpleXMLElement($xml);
          // MODS, set.
          if ($s_xml->getName() == 'mods') {
            // XXX: Should probably create a resource entry.
            $mods_datastream->setContentFromFile($this->objectInfo['xml']->uri, FALSE);
          }
          // MARCXML, transform to MODS and set.
          elseif (in_array($s_xml->getName(), array('collection', 'record'))) {
            $mods_datastream->content = static::runXslTransform(array(
              'input' => $xml,
              'xsl' => $dir . '/transforms/MARC21slim2MODS3-4.xsl',
            ));
          }
          // DC, transform to MODS and set.
          elseif ($s_xml->getName() == 'dc') {
            $mods_datastream->content = static::runXslTransform(array(
              'input' => $xml,
              'xsl' => $dir . '/transforms/simpleDC2MODS.xsl',
            ));
          }
          // Unrecognized format...
          else {
            // Do nothing?
          }
        }
      }

      // We use some Java code from LoC to transform binary MARC to MODS.
      if (!isset($mods_datastream->content) && isset($this->objectInfo['mrc'])) {
        // Generate some MODS from binary MARC.
        // Get the java executable.
        $java = variable_get('islandora_batch_java', 'java');
        $temp_file_marc = tempnam('/tmp', 'binmrc');
        $temp_file_mods = tempnam('/tmp', 'mods');

        // Copy the file, so we're sure it's accessible on the filesystem.
        copy($this->objectInfo['mrc']->uri, $temp_file_marc);
        $absolute = realpath($dir);

        // The command to run.
        $command = "$java -cp $absolute/transforms/marcxml/marcxml.jar:$absolute/transforms/marcxml/marc4j.jar gov.loc.marcxml.MARC2MODS $temp_file_marc $temp_file_mods";

        // Run the command.
        $proc = popen($command, 'r');
        pclose($proc);

        // Load the output into a DOMDocument.
        $doc = new DOMDocument();
        $doc->load($temp_file_mods);

        // Drop the temp.
        unlink($temp_file_marc);
        unlink($temp_file_mods);

        $mods_docs = $doc->getElementsByTagName('mods');
        if ($mods_docs->length > 0) {
          // Grab the first.
          $mods = new DOMDocument();
          $mods_node = $mods_docs->item(0);
          $mods_node = $mods->importNode($mods_node, TRUE);
          $mods->appendChild($mods_node);

          // TODO: Transform dates to Solr format...  Or should this be left to
          // a core hook?
          // And put the content into the datastream.
          $mods_datastream->content = $mods->saveXML();
        }
      }

      if (!isset($mods_datastream->content)) {
        // If we do not yet have any XML, create a dumb MODS with just the
        // title, and set it...
        $title = $this->baseName;

        $mods_datastream->content = <<<EOXML
<mods xmlns:mods="http://www.loc.gov/mods/v3" xmlns="http://www.loc.gov/mods/v3">
  <titleInfo>
    <title>$title</title>
  </titleInfo>
</mods>
EOXML;
      }

      $this->ingestDatastream($mods_datastream);
    }

    return $this['MODS']->content;
  }
  /**
   * Function to get dc.
   */
  protected function getDc() {
    if (!isset($this['DC'])) {
      $ingest_dc = FALSE;
      // TODO: Create the DC datastream on this object.
      $dc_datastream = $this->constructDatastream('DC', 'M');
      $dc_datastream->mimetype = 'application/xml';
      $dc_datastream->label = 'DC Record';

      if (isset($this->objectInfo['xml'])) {
        $xml = file_get_contents($this->objectInfo['xml']->uri);

        if ($xml) {
          $s_xml = new SimpleXMLElement($xml);
          if ($s_xml->getName() == 'dc') {
            // XXX: Should probably create a resource entry.
            $dc_datastream->setContentFromFile($this->objectInfo['xml']->uri, FALSE);
            $ingest_dc = TRUE;
          }
        }
      }

      // Get the DC by transforming from MODS.
      if (!isset($dc_datastream->content)) {
        // XXX: Might want to make this use a file, instead of staging the DC
        // in the database table (inside the object we serialize).
        $mods_content = $this->getMods();
        if ($mods_content) {
          $new_dc = static::runXslTransform(
            array(
              'xsl' => $this->modsToDcTransform,
              'input' => $mods_content,
            )
          );
        }
        if (isset($new_dc)) {
          $ingest_dc = TRUE;
          $dc_datastream->content = $new_dc;
        }
      }
      if ($ingest_dc) {
        $this->ingestDatastream($dc_datastream);
      }
    }

    return isset($this['DC']) ? $this['DC']->content : FALSE;
  }

  /**
   * Fetch a valid parent RELS-EXT relationship for cmodel pair.
   */
  protected function setValidRelationships() {
    // @TODO this should make use of Ontology Solution Pack instead of hardcoding this mess
    $parent_pid = $this->objectInfo['parent'];
    if (islandora_is_valid_pid($parent_pid)) {
      // I need to know what CMODEL the parent has. This is pre-calculated by the preprocessor
      $parent_cmodel = (array) $this->objectInfo['parent_cmodel'];
      //We can only get one for now
      switch(reset($parent_cmodel)) {
        case 'islandora:bookCModel':
        case 'islandora:newspaperIssueCModel':
          $this->setValidPagedRelationships($parent_pid);
        break;
        case 'islandora:newspaperCModel':
        $this->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOf', $parent_pid);
        $this->relationships->add(
          ISLANDORA_RELS_EXT_URI,
          'isSequenceNumber',
          trim($this->objectInfo['data'][$this->preprocessorParameters['object_maping']['sequencemap_row']['sequencemap']]),
          RELS_TYPE_PLAIN_LITERAL
        );
        case 'islandora:collectionCModel':
        $this->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOfCollection',$parent_pid);
        break;
        case 'islandora:compoundCModel':
         //Compund or any other sine compound allows also free linking
        default:
        $escaped_pid = str_replace(':', '_', $parent_pid);
        $rels_predicate = variable_get('islandora_compound_object_relationship', 'isConstituentOf');
        $this->relationships->add(FEDORA_RELS_EXT_URI, $rels_predicate, $parent_pid);
        $this->relationships->add(
          ISLANDORA_RELS_EXT_URI, 
          "isSequenceNumberOf$escaped_pid",
          trim($this->objectInfo['data'][$this->preprocessorParameters['object_maping']['sequencemap_row']['sequencemap']]),
          RELS_TYPE_PLAIN_LITERAL
        );
        break;
      }
    }
  }

   protected function setValidPagedRelationships($parent_pid) {
    $this->relationships->add(ISLANDORA_RELS_EXT_URI, 'isPageOf',$parent_pid);
    $this->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOf', $parent_pid);
    $this->relationships->add(ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', trim($this->objectInfo['data'][$this->preprocessorParameters['object_maping']['sequencemap_row']['sequencemap']]), RELS_TYPE_PLAIN_LITERAL);
    $this->relationships->add(ISLANDORA_RELS_EXT_URI, 'isPageNumber', trim($this->objectInfo['data'][$this->preprocessorParameters['object_maping']['sequencemap_row']['sequencemap']]), RELS_TYPE_PLAIN_LITERAL);
    $this->relationships->add(ISLANDORA_RELS_EXT_URI, 'isSection', '1', RELS_TYPE_PLAIN_LITERAL);
    // @TODO fetch the language from somewhere?
    $this->relationships->add(ISLANDORA_RELS_EXT_URI, 'hasLanguage', 'en', RELS_TYPE_PLAIN_LITERAL);
  }

  /**
   * Add collection and content model relationships.
   */
  public function addRelationships() {
    $this->setValidRelationships();
    $this->addContentModelRelationships();
    $this->inheritXacmlPolicies();
  }

  /**
   * Add inheritXacmlFrom relationship.
   */
  protected function inheritXacmlPolicies() {
    if (module_exists('islandora_xacml_editor')) {
      $collection = $this->objectInfo['parent'];
      $parent = islandora_object_load($collection);
      if ($parent) {
        islandora_xacml_editor_apply_parent_policy($this, $parent);
      }
    }
  }

  /**
   * Add the content model relationship(s).
   */
  protected function addContentModelRelationships() {
    $this->models = $this->objectInfo['cmodel'];
  }

}