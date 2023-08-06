<script setup>
import { data } from '../data/doveadm.data.js'
import MarkdownIt from 'markdown-it'

const md = new MarkdownIt()
</script>

<template>
  <template v-for="v in data.doveadm">
    <h3 :id="v.cmd" tabindex="-1">
      <code>{{ v.cmd }} {{ v.args }}</code>
      <a class="header-anchor" :href="'#' + v.cmd"></a>
    </h3>

    <span v-html="md.render(v.summary)"></span>

    <table>
      <thead>
        <tr>
          <th>Key</th>
          <th>Value</th>
        </tr>
      </thead>
      <tbody>
        <template v-for="(v2, k2) in v.fields">
          <tr>
            <td><code>{{ k2 }}</code></td>
            <td><span v-html="md.renderInline(v2)"></span></td>
          </tr>
        </template>
      </tbody>
    </table>
  </template>
</template>
