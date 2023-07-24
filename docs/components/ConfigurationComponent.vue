<script setup>
defineProps(['config'])

import MarkdownIt from 'markdown-it'
const md = new MarkdownIt()
</script>

<template>

<ul>
  <li>Default: <code>{{ config.default }}</code></li>
  <li>Value: <span v-html="md.renderInline(config.value)"></span></li>
</ul>

<span v-html="md.render(config.summary)"></span>

</template>
