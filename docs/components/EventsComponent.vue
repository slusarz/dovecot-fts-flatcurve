<script setup>
import { data } from '../data/events.data.js'
import MarkdownIt from 'markdown-it'

const md = new MarkdownIt()
</script>

<template>
  <template v-for="(v, k) in data">
    <h3 :id="k" tabindex="-1">
      <code>{{ k }}</code>
      <a class="header-anchor" :href="'#' + k"></a>
    </h3>

    <span v-html="md.render(v.summary)"></span>

    <table>
      <thead>
        <tr>
          <th>Field</th>
          <th>Description</th>
          <template v-if="v.options">
            <th>Options</th>
          </template>
        </tr>
      </thead>
      <tbody>
        <template v-for="(v2, k2) in v.fields">
          <tr>
            <td><code>{{ k2 }}</code></td>
            <td><span v-html="md.renderInline(v2)"></span></td>
            <template v-if="v.options">
              <td>
                <template v-if="k2 in v.options">
                  <code>{{ v.options[k2].join(', ') }}</code>
                </template>
              </td>
            </template>
          </tr>
        </template>
      </tbody>
    </table>
  </template>
</template>
