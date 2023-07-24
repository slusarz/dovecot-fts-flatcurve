---
layout: doc
---

<script setup>
import { data } from './configuration.data.js'
import ConfigurationComponent from './components/ConfigurationComponent.vue'
</script>

# Configuration

See [Dovecot FTS Configuration](https://doc.dovecot.org/configuration_manual/fts/) for configuration information regarding general FTS plugin options.

::: info NOTE

Flatcurve REQUIRES the core [Dovecot FTS stemming](https://doc.dovecot.org/configuration_manual/fts/tokenization/) feature.

:::

## FTS-Flatcurve Plugin Settings

**The default parameters should be fine for most people.**

<template v-for="(v, k) in data">

## `{{ k }}`

<ConfigurationComponent :config="v" />

</template>

## FTS-Flatcurve Plugin Settings Example

```
mail_plugins = $mail_plugins fts fts_flatcurve

plugin {
  fts = flatcurve

  # Recommended default FTS core configuration
  fts_filters = normalizer-icu snowball stopwords
  fts_filters_en = lowercase snowball english-possessive stopwords

  # All of these are optional, and indicate the default values.
  # They are listed here for documentation purposes; most people should
  # not need to define/override in their config.
  fts_flatcurve_commit_limit = 500
  fts_flatcurve_max_term_size = 30
  fts_flatcurve_min_term_size = 2
  fts_flatcurve_optimize_limit = 10
  fts_flatcurve_rotate_size = 5000
  fts_flatcurve_rotate_time = 5000
  fts_flatcurve_substring_search = no
}
```
