---
layout: doc
---

<script setup>
import { data } from './events.data.js'
</script>

# Events

This plugin emits [events](https://doc.dovecot.org/admin_manual/event_design/)
with the category `fts-flatcurve` (a child of the category `fts`).

The following named events are emitted:

<template v-for="(v, k) in data">

## `{{ k }}`

{{ v.summary }}

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
      <template v-if="v.options"><th>Options</th></template>
    </tr>
  </thead>
  <tbody>
    <template v-for="(v2, k2) in v.fields">
      <tr>
        <td><code>{{ k2 }}</code></td>
        <td>{{ v2 }}</td>
        <template v-if="v.options"><td><template v-if="k2 in v.options"><code>{{ v.options[k2].join(', ') }}</code></template></td></template>
      </tr>
    </template>
  </tbody>
</table>

</template>
