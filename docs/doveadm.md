---
layout: doc
---

<script setup>
import { data } from './data/doveadm.data.js'
import DoveadmComponent from './components/DoveadmComponent.vue'
</script>

# Doveadm

This plugin implements several `fts-flatcurve` specific doveadm commands.

## Doveadm Command List

<template v-for="v in data.doveadm">

### `{{ v.cmd }} {{ v.args }}`

<DoveadmComponent :doveadm="v" />

</template>
