<?php

namespace Drupal\islandora_solr\Controller;

use Drupal\Core\Controller\ControllerBase;
use Drupal\Core\Render\RendererInterface;
use Drupal\Core\Cache\CacheableJsonResponse as JsonResponse;
use Drupal\Component\Utility\Html;
use Drupal\Component\Utility\Xss;

use Symfony\Component\DependencyInjection\ContainerInterface;
use Symfony\Component\HttpFoundation\Request;

use Drupal\islandora_solr\IslandoraSolrQueryProcessor;
use Drupal\islandora_solr\IslandoraSolrResults;
use Drupal\islandora\Controller\DefaultController as IslandoraController;

/**
 * Default controller for the islandora_solr module.
 */
class DefaultController extends ControllerBase {

  protected $renderer;
  protected $container;

  /**
   * Constructor.
   */
  public function __construct(RendererInterface $renderer, ContainerInterface $container) {
    $this->renderer = $renderer;
    $this->container = $container;
  }

  /**
   * Dependency Injection.
   */
  public static function create(ContainerInterface $container) {
    return new static(
      $container->get('renderer'),
      $container
    );
  }

  /**
   * Page callback: Islandora Solr.
   *
   * Gathers url parameters, and calls the query builder, which prepares the
   * query based on the admin settings and url values.
   * Finds the right display and calls the IslandoraSolrResults class to build
   * the display, which it returns to the page.
   *
   * @param string $query
   *   The Solr query string.
   * @param array $params
   *   The URL query array.
   *
   * @global IslandoraSolrQueryProcessor $_islandora_solr_queryclass
   *   The IslandoraSolrQueryProcessor object which includes the current query
   *   settings and the raw Solr results.
   *
   * @return string
   *   A rendered Solr display
   */
  public function islandoraSolr($query = NULL, array $params = NULL) {
    global $_islandora_solr_queryclass;

    // Url parameters.
    if ($params === NULL) {
      $params = $_GET;
    }
    // Get profiles.
    $primary_profiles = $this->moduleHandler()->invokeAll('islandora_solr_primary_display');
    $secondary_profiles = $this->moduleHandler()->invokeAll('islandora_solr_secondary_display');

    // Get the preferred display profile.
    // Order:
    // - $_GET['display'].
    // - The default primary display profile.
    // - Third choice is the base IslandoraSolrResults.
    $enabled_profiles = [];
    // Get enabled displays.
    $primary_display_array = $this->config('islandora_solr.settings')->get('islandora_solr_primary_display_table');
    // If it's set, we take these values.
    if (isset($primary_display_array['enabled'])) {
      foreach ($primary_display_array['enabled'] as $key => $value) {
        if ($value) {
          $enabled_profiles[] = $key;
        }
      }
    }
    // Set primary display.
    // Check if display param is an valid, enabled profile; otherwise, show
    // default.
    if (isset($params['display']) && in_array($params['display'], $enabled_profiles)) {
      $islandora_solr_primary_display = $params['display'];
    }
    else {
      $islandora_solr_primary_display = $this->config('islandora_solr.settings')->get('islandora_solr_primary_display');
      // Unset invalid parameter.
      unset($params['display']);
    }
    $params['islandora_solr_search_navigation'] = $this->config('islandora_solr.settings')->get('islandora_solr_search_navigation');

    // !!! Set the global variable. !!!
    $_islandora_solr_queryclass = new IslandoraSolrQueryProcessor();
    $this->container->set('islandora_solr.page_query', $_islandora_solr_queryclass);

    // Build and execute Apache Solr query.
    $_islandora_solr_queryclass->buildAndExecuteQuery($query, $params);

    if (empty($_islandora_solr_queryclass->islandoraSolrResult)) {
      return $this->t('Error searching Solr index.');
    }
    // TODO: Also filter secondary displays against those checked in the
    // configuration options.
    if (isset($params['solr_profile']) && isset($secondary_profiles[$params['solr_profile']])) {
      $profile = $secondary_profiles[$_GET['solr_profile']];
    }
    elseif (isset($primary_profiles[$islandora_solr_primary_display])) {
      $profile = $primary_profiles[$islandora_solr_primary_display];
    }
    else {
      drupal_set_message(Html::escape($this->t('There is an error in the Solr search configuration: the display profile is not found.')), 'error');
      $profile = $primary_profiles['default'];
    }

    if (isset($profile['file'])) {
      // Include the file for the display profile.
      require_once drupal_get_path('module', $profile['module']) . '/' . $profile['file'];
    }
    // Get display class and function from current display.
    $solr_class = $profile['class'];
    $solr_function = $profile['function'];

    // Check if the display's class exists.
    if (class_exists($solr_class)) {
      $implementation = new $solr_class();
      // Check if the display's method exists.
      if (method_exists($implementation, $solr_function)) {
        // Implement results.
        $output = $implementation->$solr_function($_islandora_solr_queryclass);
        return $output;
      }
    }

    // Class and method could not be found, so use default.
    $results_class = new IslandoraSolrResults();
    $output = $results_class->displayResults($_islandora_solr_queryclass);

    // Debug dump.
    if ($this->config('islandora_solr.settings')->get('islandora_solr_debug_mode') && $this->currentUser()->hasPermission('view islandora solr debug')) {
      $message = $this->t('Parameters: <br /><pre>@debug</pre>', [
        '@debug' => Xss::filter(print_r($_islandora_solr_queryclass->solrParams, TRUE)),
      ]);
      drupal_set_message($message, 'status');
    }
    $output['#attached']['library'][] = 'islandora_solr/islandora-solr-theme';

    $this->renderer->addCacheableDependency($output, $_islandora_solr_queryclass);

    return $output;
  }

  /**
   * Admin autocomplete callback which returns solr fields from Luke.
   */
  public function autocompleteLuke(Request $request) {
    module_load_include('inc', 'islandora_solr', 'includes/luke');
    $string = $request->query->get('q');
    $luke = islandora_solr_get_luke();
    $result = [];
    foreach ($luke['fields'] as $term => $value) {
      if (stripos($term, $string) !== FALSE) {
        // Search case insensitive, but keep the case on replace.
        $term_str = preg_replace("/$string/i", "<strong>\$0</strong>", $term);

        // Add strong elements to highlight the found string.
        $result[] = [
          'label' => $term_str . '<strong style="float:right;">(' . $value['type'] . ')</strong>',
          'value' => $term,
        ];
      }
    }
    // Sort alphabetically.
    // @XXX: Sorting arrays isn't documented well http://php.net/manual/en/function.sort.php#54903 .
    sort($result);

    $response = new JsonResponse($result);

    $response->getCacheableMetadata()
      ->addCacheTags([
        IslandoraController::LISTING_TAG,
      ])
      ->addCacheContexts([
        'url.query_args:q',
      ]);

    return $response;
  }

}
