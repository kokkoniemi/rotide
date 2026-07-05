<!-- user card component -->
<template>
  <div class="card" :id="cardId">
    <h1 v-if="title">{{ title }}</h1>
    <ul>
      <li v-for="item in items" :key="item.id">
        {{ item.name }}
      </li>
    </ul>
    <button @click="onClick">Add</button>
  </div>
</template>

<script>
export default {
  props: ["title"],
  data() {
    return { items: [] };
  },
  methods: {
    onClick() {
      this.items.push({ id: 1, name: "new" });
    },
  },
};
</script>

<style>
.card {
  color: red;
  padding: 8px;
}
</style>
