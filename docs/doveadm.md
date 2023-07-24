---
layout: doc
---

<script setup>
import { data } from './data/doveadm.data.js'
import DoveadmComponent from './components/DoveadmComponent.vue'
</script>

# doveadm Commands

This plugin implements several `fts-flatcurve` specific doveadm commands.

<template v-for="v in data.doveadm">

## `{{ v.cmd }} {{ v.args }}`

<DoveadmComponent :doveadm="v" />

</template>
