---
layout: doc
---

<script setup>
import { data } from './data/configuration.data.js'
import ConfigurationComponent from './components/ConfigurationComponent.vue'
</script>

# Configuration

See [Dovecot FTS Configuration](https://doc.dovecot.org/configuration_manual/fts/) for configuration information regarding general FTS plugin options.

::: info NOTE

Flatcurve REQUIRES the core [Dovecot FTS stemming](https://doc.dovecot.org/configuration_manual/fts/tokenization/) feature.

:::

## Required Configuration

The following is required to enable FTS Flatcurve in Dovecot configuration:

```
# Enable both "fts" and "fts_flatcurve" plugins
mail_plugins = $mail_plugins fts fts_flatcurve

plugin {
  # Define "flatcurve" as the FTS driver.
  fts = flatcurve

  # OPTIONAL: Recommended default FTS core configuration
  fts_filters = normalizer-icu snowball stopwords
  fts_filters_en = lowercase snowball english-possessive stopwords
}
```

## Plugin Settings

**The default parameters should be fine for most people.**

These settings must go in a `plugin {}` block.  Example:

```
plugin {
  fts_flatcurve_<setting> = <setting_value>
}
```

<template v-for="(v, k) in data">

### `{{ k }}`

<ConfigurationComponent :config="v" />

</template>
