<script setup>
import { data } from '../data/configuration.data.js'
import MarkdownIt from 'markdown-it'

const md = new MarkdownIt()
</script>

<template>
  <template v-for="(v, k) in data">
    <h3 :id="k" tabindex="-1">
      <code>{{ k }}</code>
      <a class="header-anchor" :href="'#' + k"></a>
    </h3>

    <ul>
      <li>Default: <code>{{ v.default }}</code></li>
      <li>Value: <span v-html="md.renderInline(v.value)"></span></li>
    </ul>

    <span v-html="md.render(v.summary)"></span>
  </template>
</template>
