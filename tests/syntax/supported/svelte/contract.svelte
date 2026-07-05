<!-- greeting component -->
<script>
  export let items = [];
  let count = 0;
</script>

<h1 class="title" id="main">Items</h1>

{#if count > 0}
  <p>Has items</p>
{:else}
  <p>Empty</p>
{/if}

{#each items as item, index}
  <li>{item} at {index}</li>
{/each}

{#await promise}
  <span>loading</span>
{:then value}
  <span>{value}</span>
{:catch error}
  <span>{error}</span>
{/await}

{@html rawHtml}
{@const total = count + 1}

<style>
  .title {
    color: red;
    font-size: 2em;
  }
</style>
