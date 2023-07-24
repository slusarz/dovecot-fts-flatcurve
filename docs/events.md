---
layout: doc
---

<script setup>
import { data } from './data/events.data.js'
import EventsComponent from './components/EventsComponent.vue'
</script>

# Events

This plugin emits [events](https://doc.dovecot.org/admin_manual/event_design/)
with the category `fts-flatcurve` (a child of the category `fts`).

## Event List

The following named events are emitted:

<template v-for="(v, k) in data">

### `{{ k }}`

<EventsComponent :event="v" />

</template>
