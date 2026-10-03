import { defineConfig } from 'astro/config';
import { unified } from '@astrojs/markdown-remark';

const docSlug = (value) =>
  value
    .replace(/\\/g, '/')
    .split('/')
    .pop()
    .replace(/\.md$/i, '')
    .replace(/_/g, '-')
    .toLowerCase();

function rewriteRepositoryDocLinks() {
  return (tree) => {
    const walk = (node) => {
      if (node?.type === 'link' && typeof node.url === 'string') {
        const match = node.url.match(/^([^?#]+\.md)([?#].*)?$/i);
        if (match && !match[1].startsWith('/')) {
          node.url = `/docs/${docSlug(match[1])}/${match[2] ?? ''}`;
        }
      }
      if (Array.isArray(node?.children)) node.children.forEach(walk);
    };
    walk(tree);
  };
}

export default defineConfig({
  markdown: {
    processor: unified({
      remarkPlugins: [rewriteRepositoryDocLinks]
    }),
    shikiConfig: {
      theme: 'github-dark-default'
    }
  }
});
