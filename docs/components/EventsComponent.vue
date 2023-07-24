<script setup>
defineProps(['event'])

import MarkdownIt from 'markdown-it'
const md = new MarkdownIt()
</script>

<template>

<span v-html="md.render(event.summary)"></span>

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
      <template v-if="event.options"><th>Options</th></template>
    </tr>
  </thead>
  <tbody>
    <template v-for="(v2, k2) in event.fields">
      <tr>
        <td><code>{{ k2 }}</code></td>
        <td><span v-html="md.renderInline(v2)"></span></td>
        <template v-if="event.options"><td><template v-if="k2 in event.options"><code>{{ event.options[k2].join(', ') }}</code></template></td></template>
      </tr>
    </template>
  </tbody>
</table>

</template>
