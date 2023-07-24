<script setup>
defineProps(['doveadm'])

import MarkdownIt from 'markdown-it'
const md = new MarkdownIt()
</script>

<template>

<span v-html="md.render(doveadm.summary)"></span>

<table>
  <thead>
    <tr>
      <th>Key</th>
      <th>Value</th>
    </tr>
  </thead>
  <tbody>
    <template v-for="(v, k) in doveadm.fields">
      <tr>
        <td><code>{{ k }}</code></td>
        <td><span v-html="md.renderInline(v)"></span></td>
      </tr>
    </template>
  </tbody>
</table>

</template>
