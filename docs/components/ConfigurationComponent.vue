<script setup>
defineProps(['config'])
</script>

<template>

<ul>
  <li>Default: <code>{{ config.default }}</code></li>
  <li>Value: <span v-html="config.value"></span></li>
</ul>

<p>{{ config.summary }}</p>

</template>
