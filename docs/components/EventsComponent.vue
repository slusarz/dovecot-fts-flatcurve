<script setup>
defineProps(['event'])
</script>

<template>

<p>{{ event.summary }}</p>

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
        <td>{{ v2 }}</td>
        <template v-if="event.options"><td><template v-if="k2 in event.options"><code>{{ event.options[k2].join(', ') }}</code></template></td></template>
      </tr>
    </template>
  </tbody>
</table>

</template>
