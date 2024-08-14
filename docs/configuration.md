---
layout: doc
---

<script setup>
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

  # These are not flatcurve settings, but required for Dovecot FTS. See
  # Dovecot FTS Configuration link above for further information.
  fts_languages = en es de
  fts_tokenizers = generic email-address

  # Maximum term length can be set via the 'maxlen' argument (maxlen is
  # specified in bytes, not number of UTF-8 characters)
  fts_tokenizer_email_address = maxlen=100
  fts_tokenizer_generic = algorithm=simple maxlen=30

  # OPTIONAL: Recommended default FTS core configuration
  fts_filters = normalizer-icu snowball stopwords
  fts_filters_en = lowercase snowball english-possessive stopwords
}
```

### NFS Recommendation

To prevent concurrent writes to the Xapian database files, Dovecot relies on file locking.

If using NFS, the [`VOLATILEDIR`](https://doc.dovecot.org/configuration_manual/nfs/#optimizations) parameter for the [`mail_location`](https://doc.dovecot.org/configuration_manual/mail_location/) configuration option should be used to perform this locking locally as opposed to on the remote server.

## Plugin Settings

**The default parameters should be fine for most people.**

These settings must go in a `plugin {}` block.  Example:

```
plugin {
  fts_flatcurve_<setting> = <setting_value>
}
```

<ConfigurationComponent />
